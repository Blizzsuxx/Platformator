#include "physics/physicsmanager.h"

#include "core/constants.h"
#include "core/membership.h"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>

namespace
{
bool hasWakeMotion(const Rigidbody *other) noexcept
{
    return other->getVelocity().squaredNorm() > LINEAR_SLEEP_THRESHOLD * LINEAR_SLEEP_THRESHOLD ||
           std::abs(other->getAngularVelocity()) > ANGULAR_SLEEP_THRESHOLD ||
           (other->getBodyType() == BodyType::KINEMATIC &&
            (!other->getVelocity().isZero(0) || other->getAngularVelocity() != 0.0f));
}
} // namespace

// ============================================================================
// Persistent Island Lifecycle & Color-Graph Management
// ============================================================================

int32_t PhysicsManager::acquirePersistentIsland()
{
    int32_t id = ColliderPair::INVALID_ISLAND_ID;
    if (!freeIslandIds.empty())
    {
        id = freeIslandIds.back();
        freeIslandIds.pop_back();
    }
    else
    {
        id = static_cast<int32_t>(persistentIslands.size());
        persistentIslands.emplace_back();
    }
    PersistentIsland &island = persistentIslands[static_cast<size_t>(id)];
    island.id = id;
    island.activeListIndex = SIZE_MAX;
    island.bodies.clear();
    island.contacts.clear();
    island.colorGraph.reset();
    island.splitCooldown = 0;
    island.maySplit = false;
    island.splitQueued = false;
    island.hasSleepingEndpoint = false;
    return id;
}

void PhysicsManager::clearIslandColorGraph(PersistentIsland &island)
{
    if (!island.colorGraph)
    {
        return;
    }
    for (Rigidbody *body : island.bodies)
    {
        body->solverColorMask = 0;
    }
    for (ColliderPair *pair : island.contacts)
    {
        pair->solverColor = ColliderPair::UNCOLORED;
        pair->solverColorSlot = ColliderPair::INVALID_INDEX;
    }
    island.colorGraph.reset();
}

void PhysicsManager::insertContactIntoColorGraph(IslandColorGraph &graph, ColliderPair *pair)
{
    if (pair->solverColor != ColliderPair::UNCOLORED)
    {
        return;
    }
    const auto [rbA, rbB] = pairRigidbodies(pair);
    const bool dynA = rbA != nullptr && rbA->getBodyType() == BodyType::DYNAMIC;
    const bool dynB = rbB != nullptr && rbB->getBodyType() == BodyType::DYNAMIC;
    const uint32_t usedMask = (dynA ? rbA->solverColorMask : 0u) | (dynB ? rbB->solverColorMask : 0u);
    constexpr uint32_t allColorsMask = (1u << IslandColorGraph::MAX_PARALLEL_COLORS) - 1u;
    const uint32_t freeMask = (~usedMask) & allColorsMask;
    uint8_t color = IslandColorGraph::OVERFLOW_COLOR;
    if (freeMask != 0u)
    {
        color = static_cast<uint8_t>(std::countr_zero(freeMask));
        const uint32_t bit = 1u << color;
        if (dynA)
        {
            rbA->solverColorMask |= bit;
        }
        if (dynB)
        {
            rbB->solverColorMask |= bit;
        }
    }
    auto &bucket = graph.buckets[color];
    pair->solverColor = color;
    pair->solverColorSlot = static_cast<uint32_t>(bucket.size());
    bucket.push_back(pair);
}

void PhysicsManager::removeContactFromColorGraph(IslandColorGraph &graph, ColliderPair *pair)
{
    const uint8_t color = pair->solverColor;
    if (color == ColliderPair::UNCOLORED)
    {
        return;
    }
    auto &bucket = graph.buckets[color];
    const uint32_t slot = pair->solverColorSlot;
    if (slot < bucket.size())
    {
        const uint32_t last = static_cast<uint32_t>(bucket.size() - 1);
        if (slot != last)
        {
            ColliderPair *moved = bucket[last];
            bucket[slot] = moved;
            moved->solverColorSlot = slot;
        }
        bucket.pop_back();
    }
    pair->solverColor = ColliderPair::UNCOLORED;
    pair->solverColorSlot = ColliderPair::INVALID_INDEX;
    if (color < IslandColorGraph::MAX_PARALLEL_COLORS)
    {
        const uint32_t clearMask = ~(1u << color);
        const auto [rbA, rbB] = pairRigidbodies(pair);
        if (rbA != nullptr && rbA->getBodyType() == BodyType::DYNAMIC)
        {
            rbA->solverColorMask &= clearMask;
        }
        if (rbB != nullptr && rbB->getBodyType() == BodyType::DYNAMIC)
        {
            rbB->solverColorMask &= clearMask;
        }
    }
}

void PhysicsManager::ensureIslandColorGraph(PersistentIsland &island)
{
    if (island.colorGraph)
    {
        return;
    }
    island.colorGraph = std::make_unique<IslandColorGraph>();
    for (Rigidbody *body : island.bodies)
    {
        body->solverColorMask = 0;
    }
    for (ColliderPair *pair : island.contacts)
    {
        pair->solverColor = ColliderPair::UNCOLORED;
        pair->solverColorSlot = ColliderPair::INVALID_INDEX;
        insertContactIntoColorGraph(*island.colorGraph, pair);
    }
}

void PhysicsManager::setPersistentIslandActive(PersistentIsland &island, bool active)
{
    if (active)
    {
        if (island.activeListIndex == SIZE_MAX)
        {
            island.activeListIndex = activeIslandIds.size();
            activeIslandIds.push_back(island.id);
        }
    }
    else if (island.activeListIndex != SIZE_MAX)
    {
        swapRemoveIndexed(
            activeIslandIds, island.id,
            [&](int32_t id) { return persistentIslands[static_cast<size_t>(id)].activeListIndex; },
            [&](int32_t id, size_t idx) { persistentIslands[static_cast<size_t>(id)].activeListIndex = idx; });
    }
}

void PhysicsManager::recyclePersistentIsland(int32_t islandId)
{
    PersistentIsland &island = persistentIslands[static_cast<size_t>(islandId)];
    island.colorGraph.reset();
    setPersistentIslandActive(island, false);
    for (Rigidbody *body : island.bodies)
    {
        body->solverIslandId = ColliderPair::INVALID_ISLAND_ID;
        body->solverIslandBodyIndex = Rigidbody::INVALID_INDEX;
        body->solverColorMask = 0;
    }
    for (ColliderPair *pair : island.contacts)
    {
        pair->solverIslandId = ColliderPair::INVALID_ISLAND_ID;
        pair->solverIslandContactIndex = ColliderPair::INVALID_INDEX;
        pair->solverColor = ColliderPair::UNCOLORED;
        pair->solverColorSlot = ColliderPair::INVALID_INDEX;
        pair->cachedRbA = nullptr;
        pair->cachedRbB = nullptr;
    }
    island.bodies.clear();
    island.contacts.clear();
    island.splitCooldown = 0;
    island.maySplit = false;
    island.hasSleepingEndpoint = false;
    island.id = ColliderPair::INVALID_ISLAND_ID;
    freeIslandIds.push_back(islandId);
}

void PhysicsManager::queuePersistentIslandSplit(PersistentIsland &island)
{
    island.maySplit = true;
    if (!island.splitQueued && island.id != ColliderPair::INVALID_ISLAND_ID)
    {
        island.splitQueued = true;
        dirtySplitIslandIds.push_back(island.id);
    }
}

// ============================================================================
// Body & Contact Island Membership (O(1) amortized on enter/exit)
// ============================================================================

void PhysicsManager::addBodyToPersistentIsland(PersistentIsland &island, Rigidbody *body)
{
    assert(body->solverIslandId == ColliderPair::INVALID_ISLAND_ID);
    body->solverIslandId = island.id;
    body->solverIslandBodyIndex = static_cast<uint32_t>(island.bodies.size());
    island.bodies.push_back(body);
}

void PhysicsManager::detachBodyFromPersistentIsland(Rigidbody *body)
{
    const int32_t islandId = body->solverIslandId;
    if (islandId == ColliderPair::INVALID_ISLAND_ID)
    {
        return;
    }
    PersistentIsland &island = persistentIslands[static_cast<size_t>(islandId)];
    if (body->solverColorMask != 0)
    {
        clearIslandColorGraph(island);
    }
    membership(island.bodies, body, &Rigidbody::solverIslandBodyIndex, false);
    body->solverIslandId = ColliderPair::INVALID_ISLAND_ID;
    body->solverColorMask = 0;

    if (island.bodies.empty() || island.contacts.empty())
    {
        recyclePersistentIsland(islandId);
    }
    else
    {
        island.splitCooldown = 0;
        queuePersistentIslandSplit(island);
    }
}

int32_t PhysicsManager::mergePersistentIslands(int32_t islandIdA, int32_t islandIdB)
{
    if (islandIdA == islandIdB)
    {
        return islandIdA;
    }
    PersistentIsland &a = persistentIslands[static_cast<size_t>(islandIdA)];
    PersistentIsland &b = persistentIslands[static_cast<size_t>(islandIdB)];
    int32_t keepId = islandIdA;
    int32_t dropId = islandIdB;
    const bool aColored = a.colorGraph != nullptr;
    const bool bColored = b.colorGraph != nullptr;
    if ((bColored && !aColored) ||
        (aColored == bColored && a.bodies.size() + a.contacts.size() < b.bodies.size() + b.contacts.size()))
    {
        keepId = islandIdB;
        dropId = islandIdA;
    }

    PersistentIsland &keep = persistentIslands[static_cast<size_t>(keepId)];
    PersistentIsland &drop = persistentIslands[static_cast<size_t>(dropId)];
    clearIslandColorGraph(drop);

    keep.bodies.reserve(keep.bodies.size() + drop.bodies.size());
    for (Rigidbody *body : drop.bodies)
    {
        body->solverIslandId = keepId;
        body->solverIslandBodyIndex = static_cast<uint32_t>(keep.bodies.size());
        body->solverColorMask = 0;
        keep.bodies.push_back(body);
    }
    keep.contacts.reserve(keep.contacts.size() + drop.contacts.size());
    for (ColliderPair *pair : drop.contacts)
    {
        pair->solverIslandId = keepId;
        pair->solverIslandContactIndex = static_cast<uint32_t>(keep.contacts.size());
        keep.contacts.push_back(pair);
        if (keep.colorGraph)
        {
            insertContactIntoColorGraph(*keep.colorGraph, pair);
        }
    }
    keep.hasSleepingEndpoint |= drop.hasSleepingEndpoint;
    if (drop.maySplit)
    {
        queuePersistentIslandSplit(keep);
    }

    drop.bodies.clear();
    drop.contacts.clear();
    recyclePersistentIsland(dropId);
    setPersistentIslandActive(keep, !keep.contacts.empty());
    return keepId;
}

void PhysicsManager::removeContactFromPersistentIsland(ColliderPair *pair)
{
    const int32_t islandId = pair->solverIslandId;
    if (islandId == ColliderPair::INVALID_ISLAND_ID)
    {
        return;
    }
    PersistentIsland &island = persistentIslands[static_cast<size_t>(islandId)];
    if (island.colorGraph)
    {
        removeContactFromColorGraph(*island.colorGraph, pair);
    }
    membership(island.contacts, pair, &ColliderPair::solverIslandContactIndex, false);
    pair->solverIslandId = ColliderPair::INVALID_ISLAND_ID;
    pair->cachedRbA = nullptr;
    pair->cachedRbB = nullptr;

    if (island.contacts.empty())
    {
        recyclePersistentIsland(islandId);
    }
    else if (island.bodies.size() > 1)
    {
        queuePersistentIslandSplit(island);
    }
}

void PhysicsManager::updatePersistentIslandContact(ColliderPair *pair, bool solve,
                                                   Collision::CollisionType response, Rigidbody *rbA, Rigidbody *rbB)
{
    if (!solve)
    {
        removeContactFromPersistentIsland(pair);
        membership(kinematicSolverPairs, pair, &ColliderPair::kinematicSolverIndex, false);
        pair->cachedRbA = nullptr;
        pair->cachedRbB = nullptr;
        return;
    }

    if (response == Collision::KINEMATIC)
    {
        removeContactFromPersistentIsland(pair);
        membership(kinematicSolverPairs, pair, &ColliderPair::kinematicSolverIndex, true);
        pair->cachedRbA = rbA;
        pair->cachedRbB = rbB;
        return;
    }

    membership(kinematicSolverPairs, pair, &ColliderPair::kinematicSolverIndex, false);

    const bool dynA = rbA != nullptr && rbA->getBodyType() == BodyType::DYNAMIC;
    const bool dynB = rbB != nullptr && rbB->getBodyType() == BodyType::DYNAMIC;
    if (!dynA && !dynB)
    {
        removeContactFromPersistentIsland(pair);
        pair->cachedRbA = nullptr;
        pair->cachedRbB = nullptr;
        return;
    }

    const int32_t existingId = pair->solverIslandId;
    if (existingId != ColliderPair::INVALID_ISLAND_ID)
    {
        const bool matchA = !dynA || rbA->solverIslandId == existingId;
        const bool matchB = !dynB || rbB->solverIslandId == existingId;
        if (matchA && matchB)
        {
            pair->cachedRbA = rbA;
            pair->cachedRbB = rbB;
            if ((rbA != nullptr && rbA->getIsSleeping()) || (rbB != nullptr && rbB->getIsSleeping()))
            {
                persistentIslands[static_cast<size_t>(existingId)].hasSleepingEndpoint = true;
            }
            return;
        }
        removeContactFromPersistentIsland(pair);
    }

    int32_t targetId = ColliderPair::INVALID_ISLAND_ID;
    if (dynA && dynB)
    {
        const int32_t idA = rbA->solverIslandId;
        const int32_t idB = rbB->solverIslandId;
        if (idA == ColliderPair::INVALID_ISLAND_ID && idB == ColliderPair::INVALID_ISLAND_ID)
        {
            targetId = acquirePersistentIsland();
            PersistentIsland &island = persistentIslands[static_cast<size_t>(targetId)];
            addBodyToPersistentIsland(island, rbA);
            addBodyToPersistentIsland(island, rbB);
        }
        else if (idA != ColliderPair::INVALID_ISLAND_ID && idB == ColliderPair::INVALID_ISLAND_ID)
        {
            targetId = idA;
            addBodyToPersistentIsland(persistentIslands[static_cast<size_t>(targetId)], rbB);
        }
        else if (idA == ColliderPair::INVALID_ISLAND_ID && idB != ColliderPair::INVALID_ISLAND_ID)
        {
            targetId = idB;
            addBodyToPersistentIsland(persistentIslands[static_cast<size_t>(targetId)], rbA);
        }
        else
        {
            targetId = mergePersistentIslands(idA, idB);
        }
    }
    else
    {
        Rigidbody *dynBody = dynA ? rbA : rbB;
        if (dynBody->solverIslandId == ColliderPair::INVALID_ISLAND_ID)
        {
            targetId = acquirePersistentIsland();
            addBodyToPersistentIsland(persistentIslands[static_cast<size_t>(targetId)], dynBody);
        }
        else
        {
            targetId = dynBody->solverIslandId;
        }
    }

    PersistentIsland &island = persistentIslands[static_cast<size_t>(targetId)];
    pair->cachedRbA = rbA;
    pair->cachedRbB = rbB;
    pair->solverIslandId = targetId;
    pair->solverIslandContactIndex = static_cast<uint32_t>(island.contacts.size());
    island.contacts.push_back(pair);
    if (island.colorGraph)
    {
        insertContactIntoColorGraph(*island.colorGraph, pair);
    }
    if ((rbA != nullptr && rbA->getIsSleeping()) || (rbB != nullptr && rbB->getIsSleeping()))
    {
        island.hasSleepingEndpoint = true;
    }
    setPersistentIslandActive(island, true);
}

// ============================================================================
// Island Splitting & Wake Propagation
// ============================================================================

int32_t PhysicsManager::findIslandComponents(PersistentIsland &island)
{
    const size_t bodyCount = island.bodies.size();
    splitScratch.dsu.reset(bodyCount);

    size_t remainingComponents = bodyCount;
    for (const ColliderPair *pair : island.contacts)
    {
        const auto [rbA, rbB] = pairRigidbodies(pair);
        if (rbA != nullptr && rbB != nullptr && rbA->solverIslandId == island.id && rbB->solverIslandId == island.id)
        {
            if (splitScratch.dsu.unite(rbA->solverIslandBodyIndex, rbB->solverIslandBodyIndex))
            {
                if (--remainingComponents == 1)
                {
                    break;
                }
            }
        }
    }

    if (remainingComponents <= 1)
    {
        if (bodyCount >= 32)
        {
            island.splitCooldown = static_cast<uint16_t>(std::min<size_t>(24, std::max<size_t>(12, bodyCount / 8)));
        }
        return 1;
    }

    splitScratch.componentOfBody.assign(bodyCount, -1);
    int32_t componentCount = 0;
    for (size_t i = 0; i < bodyCount; ++i)
    {
        const uint32_t root = splitScratch.dsu.find(static_cast<uint32_t>(i));
        if (splitScratch.componentOfBody[root] == -1)
        {
            splitScratch.componentOfBody[root] = componentCount++;
        }
        splitScratch.componentOfBody[i] = splitScratch.componentOfBody[root];
    }
    return componentCount;
}

void PhysicsManager::partitionIslandByComponents(int32_t islandId, int32_t componentCount)
{
    PersistentIsland &island = persistentIslands[static_cast<size_t>(islandId)];
    island.splitCooldown = 0;

    // Separate bodies and contacts across connected components, preserving relative contact order
    // and keeping the largest component's existing color graph intact.
    auto &remainingContacts = splitScratch.remainingContacts;
    auto &oldBodies = splitScratch.oldBodies;
    auto &componentContactCounts = splitScratch.componentContactCounts;
    auto &contactComponents = splitScratch.contactComponents;
    auto &componentIslandIds = splitScratch.componentIslandIds;
    const auto &componentOfBody = splitScratch.componentOfBody;

    remainingContacts.clear();
    remainingContacts.swap(island.contacts);
    oldBodies.clear();
    oldBodies.swap(island.bodies);
    const bool hadSleeping = island.hasSleepingEndpoint;
    island.hasSleepingEndpoint = false;

    componentContactCounts.assign(static_cast<size_t>(componentCount), 0);
    contactComponents.resize(remainingContacts.size());
    for (size_t i = 0; i < remainingContacts.size(); ++i)
    {
        const ColliderPair *pair = remainingContacts[i];
        const auto [rbA, rbB] = pairRigidbodies(pair);
        int32_t comp = -1;
        if (rbA != nullptr && rbA->solverIslandId == islandId)
        {
            comp = componentOfBody[rbA->solverIslandBodyIndex];
        }
        else if (rbB != nullptr && rbB->solverIslandId == islandId)
        {
            comp = componentOfBody[rbB->solverIslandBodyIndex];
        }
        contactComponents[i] = comp;
        if (comp >= 0)
        {
            ++componentContactCounts[static_cast<size_t>(comp)];
        }
    }

    int32_t primaryComp = -1;
    size_t primaryContactCount = 0;
    for (int32_t comp = 0; comp < componentCount; ++comp)
    {
        const size_t count = componentContactCounts[static_cast<size_t>(comp)];
        if (count > primaryContactCount)
        {
            primaryContactCount = count;
            primaryComp = comp;
        }
    }

    componentIslandIds.assign(static_cast<size_t>(componentCount), ColliderPair::INVALID_ISLAND_ID);
    bool reusedPrimary = false;
    for (int32_t comp = 0; comp < componentCount; ++comp)
    {
        if (componentContactCounts[static_cast<size_t>(comp)] == 0)
        {
            continue;
        }
        if (comp == primaryComp)
        {
            componentIslandIds[static_cast<size_t>(comp)] = islandId;
            reusedPrimary = true;
        }
        else
        {
            componentIslandIds[static_cast<size_t>(comp)] = acquirePersistentIsland();
        }
    }

    PersistentIsland &primaryIsland = persistentIslands[static_cast<size_t>(islandId)];
    IslandColorGraph *preservedColorGraph = reusedPrimary ? primaryIsland.colorGraph.get() : nullptr;
    if (!reusedPrimary && primaryIsland.colorGraph)
    {
        clearIslandColorGraph(primaryIsland);
    }

    if (preservedColorGraph != nullptr)
    {
        for (size_t i = 0; i < remainingContacts.size(); ++i)
        {
            if (contactComponents[i] != primaryComp)
            {
                removeContactFromColorGraph(*preservedColorGraph, remainingContacts[i]);
            }
        }
    }

    // Re-home bodies (components with 0 contacts detach cleanly).
    for (size_t i = 0; i < oldBodies.size(); ++i)
    {
        Rigidbody *body = oldBodies[i];
        const int32_t comp = componentOfBody[i];
        const int32_t targetId = componentIslandIds[static_cast<size_t>(comp)];
        body->solverIslandId = ColliderPair::INVALID_ISLAND_ID;
        body->solverIslandBodyIndex = Rigidbody::INVALID_INDEX;
        if (comp != primaryComp)
        {
            body->solverColorMask = 0;
        }
        if (targetId != ColliderPair::INVALID_ISLAND_ID)
        {
            addBodyToPersistentIsland(persistentIslands[static_cast<size_t>(targetId)], body);
        }
    }

    // Re-home contacts preserving relative order.
    for (size_t i = 0; i < remainingContacts.size(); ++i)
    {
        ColliderPair *pair = remainingContacts[i];
        const int32_t comp = contactComponents[i];
        const int32_t targetId =
            comp >= 0 ? componentIslandIds[static_cast<size_t>(comp)] : ColliderPair::INVALID_ISLAND_ID;
        if (targetId != ColliderPair::INVALID_ISLAND_ID)
        {
            PersistentIsland &target = persistentIslands[static_cast<size_t>(targetId)];
            pair->solverIslandId = targetId;
            pair->solverIslandContactIndex = static_cast<uint32_t>(target.contacts.size());
            target.contacts.push_back(pair);
            if (hadSleeping)
            {
                const auto [rbA, rbB] = pairRigidbodies(pair);
                if ((rbA != nullptr && rbA->getIsSleeping()) || (rbB != nullptr && rbB->getIsSleeping()))
                {
                    target.hasSleepingEndpoint = true;
                }
            }
        }
        else
        {
            pair->solverIslandId = ColliderPair::INVALID_ISLAND_ID;
            pair->solverIslandContactIndex = ColliderPair::INVALID_INDEX;
            pair->cachedRbA = nullptr;
            pair->cachedRbB = nullptr;
        }
    }

    for (int32_t targetId : componentIslandIds)
    {
        if (targetId != ColliderPair::INVALID_ISLAND_ID)
        {
            setPersistentIslandActive(persistentIslands[static_cast<size_t>(targetId)], true);
        }
    }

    if (!reusedPrimary)
    {
        recyclePersistentIsland(islandId);
    }
}

void PhysicsManager::splitPersistentIsland(int32_t islandId)
{
    PersistentIsland &island = persistentIslands[static_cast<size_t>(islandId)];
    island.splitQueued = false;
    if (island.id == ColliderPair::INVALID_ISLAND_ID || !island.maySplit)
    {
        return;
    }
    island.maySplit = false;

    if (island.bodies.size() <= 1 || island.contacts.empty())
    {
        if (island.contacts.empty() && island.id != ColliderPair::INVALID_ISLAND_ID)
        {
            recyclePersistentIsland(islandId);
        }
        return;
    }

    const int32_t componentCount = findIslandComponents(island);
    if (componentCount > 1)
    {
        partitionIslandByComponents(islandId, componentCount);
    }
}

void PhysicsManager::splitDirtyIslands()
{
    if (dirtySplitIslandIds.empty())
    {
        return;
    }
    size_t keepCount = 0;
    const size_t count = dirtySplitIslandIds.size();
    for (size_t i = 0; i < count; ++i)
    {
        const int32_t islandId = dirtySplitIslandIds[i];
        PersistentIsland &island = persistentIslands[static_cast<size_t>(islandId)];
        if (island.id == ColliderPair::INVALID_ISLAND_ID || !island.maySplit)
        {
            island.splitQueued = false;
            continue;
        }
        if (island.splitCooldown > 0)
        {
            --island.splitCooldown;
            dirtySplitIslandIds[keepCount++] = islandId;
            continue;
        }
        splitPersistentIsland(islandId);
    }
    dirtySplitIslandIds.resize(keepCount);
}

void PhysicsManager::wakePersistentIslandEndpoints()
{
    for (Rigidbody *body : pendingBodyChanges)
    {
        if (body->solverIslandId != ColliderPair::INVALID_ISLAND_ID && body->getIsSleeping())
        {
            persistentIslands[static_cast<size_t>(body->solverIslandId)].hasSleepingEndpoint = true;
        }
    }
    for (int32_t islandId : activeIslandIds)
    {
        PersistentIsland &island = persistentIslands[static_cast<size_t>(islandId)];
        if (!island.hasSleepingEndpoint)
        {
            continue;
        }
        bool stillHasSleeping = false;
        for (ColliderPair *pair : island.contacts)
        {
            if (pair->getCollision() == nullptr)
            {
                continue;
            }
            const auto [rbA, rbB] = pairRigidbodies(pair);
            if (rbA == nullptr || rbB == nullptr)
            {
                continue;
            }
            if (rbA->getIsSleeping() && hasWakeMotion(rbB))
            {
                rbA->wakeUp();
            }
            if (rbB->getIsSleeping() && hasWakeMotion(rbA))
            {
                rbB->wakeUp();
            }
            stillHasSleeping |= rbA->getIsSleeping() || rbB->getIsSleeping();
        }
        island.hasSleepingEndpoint = stillHasSleeping;
    }
}
