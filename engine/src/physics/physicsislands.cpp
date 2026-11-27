#include "physics/physicsmanager.h"

#include "core/constants.h"
#include "core/membership.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>

namespace
{
bool hasWakeMotion(const Rigidbody *other) noexcept
{
    return other->getVelocity().squaredNorm() > LINEAR_SLEEP_THRESHOLD * LINEAR_SLEEP_THRESHOLD ||
           std::abs(other->getAngularVelocity()) > ANGULAR_SLEEP_THRESHOLD ||
           (other->getBodyType() == KINEMATIC &&
            (!other->getVelocity().isZero(0) || other->getAngularVelocity() != 0));
}
} // namespace

// ============================================================================
// Persistent Island Management (Serial, O(1) amortized on contact enter/exit)
// ============================================================================

int32_t PhysicsManager::acquirePersistentIsland()
{
    int32_t id = -1;
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
    for (const ColliderPair *pair : island.contacts)
    {
        pair->solverColor = 0xFFu;
        pair->solverColorSlot = UINT32_MAX;
    }
    island.colorGraph.reset();
}

void PhysicsManager::insertContactIntoColorGraph(IslandColorGraph &graph, const ColliderPair *pair)
{
    if (pair->solverColor != 0xFFu)
    {
        return;
    }
    auto *rbA = pair->getObjectA()->getAttachedRigidbody();
    auto *rbB = pair->getObjectB()->getAttachedRigidbody();
    const bool dynA = rbA != nullptr && rbA->getBodyType() == BodyType::DYNAMIC;
    const bool dynB = rbB != nullptr && rbB->getBodyType() == BodyType::DYNAMIC;
    const uint32_t usedMask = (dynA ? rbA->solverColorMask : 0u) | (dynB ? rbB->solverColorMask : 0u);
    constexpr uint32_t allColorsMask = (1u << IslandColorGraph::MAX_PARALLEL_COLORS) - 1u;
    const uint32_t freeMask = (~usedMask) & allColorsMask;
    uint8_t color = IslandColorGraph::OVERFLOW_COLOR;
    if (freeMask != 0u)
    {
        color = static_cast<uint8_t>(__builtin_ctz(freeMask));
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

void PhysicsManager::removeContactFromColorGraph(IslandColorGraph &graph, const ColliderPair *pair)
{
    const uint8_t color = pair->solverColor;
    if (color == 0xFFu)
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
            const ColliderPair *moved = bucket[last];
            bucket[slot] = moved;
            moved->solverColorSlot = slot;
        }
        bucket.pop_back();
    }
    pair->solverColor = 0xFFu;
    pair->solverColorSlot = UINT32_MAX;
    if (color < IslandColorGraph::MAX_PARALLEL_COLORS)
    {
        const uint32_t clearMask = ~(1u << color);
        auto *rbA = pair->getObjectA()->getAttachedRigidbody();
        auto *rbB = pair->getObjectB()->getAttachedRigidbody();
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
    for (const ColliderPair *pair : island.contacts)
    {
        pair->solverColor = 0xFFu;
        pair->solverColorSlot = UINT32_MAX;
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
    else
    {
        if (island.activeListIndex != SIZE_MAX)
        {
            const size_t removeIdx = island.activeListIndex;
            const size_t lastIdx = activeIslandIds.size() - 1;
            if (removeIdx != lastIdx)
            {
                const int32_t movedId = activeIslandIds[lastIdx];
                activeIslandIds[removeIdx] = movedId;
                persistentIslands[static_cast<size_t>(movedId)].activeListIndex = removeIdx;
            }
            activeIslandIds.pop_back();
            island.activeListIndex = SIZE_MAX;
        }
    }
}

void PhysicsManager::recyclePersistentIsland(int32_t islandId)
{
    PersistentIsland &island = persistentIslands[static_cast<size_t>(islandId)];
    clearIslandColorGraph(island);
    setPersistentIslandActive(island, false);
    for (Rigidbody *body : island.bodies)
    {
        body->solverIslandId = -1;
        body->solverIslandBodyIndex = SIZE_MAX;
        body->solverColorMask = 0;
    }
    for (const ColliderPair *pair : island.contacts)
    {
        pair->solverIslandId = -1;
        pair->solverIslandContactIndex = SIZE_MAX;
        pair->solverColor = 0xFFu;
        pair->solverColorSlot = UINT32_MAX;
    }
    island.bodies.clear();
    island.contacts.clear();
    island.maySplit = false;
    island.hasSleepingEndpoint = false;
    island.id = -1;
    freeIslandIds.push_back(islandId);
}

void PhysicsManager::queuePersistentIslandSplit(PersistentIsland &island)
{
    island.maySplit = true;
    if (!island.splitQueued && island.id != -1)
    {
        island.splitQueued = true;
        dirtySplitIslandIds.push_back(island.id);
    }
}

void PhysicsManager::addBodyToPersistentIsland(PersistentIsland &island, Rigidbody *body)
{
    assert(body->solverIslandId == -1);
    body->solverIslandId = island.id;
    body->solverIslandBodyIndex = island.bodies.size();
    island.bodies.push_back(body);
}

void PhysicsManager::detachBodyFromPersistentIsland(Rigidbody *body)
{
    const int32_t islandId = body->solverIslandId;
    if (islandId == -1)
    {
        return;
    }
    PersistentIsland &island = persistentIslands[static_cast<size_t>(islandId)];
    clearIslandColorGraph(island);
    const size_t removeIdx = body->solverIslandBodyIndex;
    const size_t lastIdx = island.bodies.size() - 1;
    if (removeIdx != lastIdx)
    {
        Rigidbody *moved = island.bodies[lastIdx];
        island.bodies[removeIdx] = moved;
        moved->solverIslandBodyIndex = removeIdx;
    }
    island.bodies.pop_back();
    body->solverIslandId = -1;
    body->solverIslandBodyIndex = SIZE_MAX;
    body->solverColorMask = 0;

    if (island.bodies.empty() || island.contacts.empty())
    {
        recyclePersistentIsland(islandId);
    }
    else
    {
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

    for (Rigidbody *body : drop.bodies)
    {
        body->solverIslandId = keepId;
        body->solverIslandBodyIndex = keep.bodies.size();
        body->solverColorMask = 0;
        keep.bodies.push_back(body);
    }
    for (const ColliderPair *pair : drop.contacts)
    {
        pair->solverIslandId = keepId;
        pair->solverIslandContactIndex = keep.contacts.size();
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

void PhysicsManager::removeContactFromPersistentIsland(const ColliderPair *pair)
{
    const int32_t islandId = pair->solverIslandId;
    if (islandId == -1)
    {
        return;
    }
    PersistentIsland &island = persistentIslands[static_cast<size_t>(islandId)];
    if (island.colorGraph)
    {
        removeContactFromColorGraph(*island.colorGraph, pair);
    }
    const size_t removeIdx = pair->solverIslandContactIndex;
    const size_t lastIdx = island.contacts.size() - 1;
    if (removeIdx != lastIdx)
    {
        const ColliderPair *moved = island.contacts[lastIdx];
        island.contacts[removeIdx] = moved;
        moved->solverIslandContactIndex = removeIdx;
    }
    island.contacts.pop_back();
    pair->solverIslandId = -1;
    pair->solverIslandContactIndex = SIZE_MAX;

    if (island.contacts.empty())
    {
        recyclePersistentIsland(islandId);
    }
    else if (island.bodies.size() > 1)
    {
        queuePersistentIslandSplit(island);
    }
}

void PhysicsManager::updatePersistentIslandContact(const ColliderPair *pair, bool solve,
                                                   Collision::CollisionType response, Rigidbody *rbA, Rigidbody *rbB)
{
    if (!solve)
    {
        removeContactFromPersistentIsland(pair);
        membership(kinematicSolverPairs, pair, &ColliderPair::kinematicSolverIndex, false);
        return;
    }

    if (response == Collision::KINEMATIC)
    {
        removeContactFromPersistentIsland(pair);
        membership(kinematicSolverPairs, pair, &ColliderPair::kinematicSolverIndex, true);
        return;
    }

    membership(kinematicSolverPairs, pair, &ColliderPair::kinematicSolverIndex, false);

    const bool dynA = rbA != nullptr && rbA->getBodyType() == BodyType::DYNAMIC;
    const bool dynB = rbB != nullptr && rbB->getBodyType() == BodyType::DYNAMIC;
    if (!dynA && !dynB)
    {
        removeContactFromPersistentIsland(pair);
        return;
    }

    const int32_t existingId = pair->solverIslandId;
    if (existingId != -1)
    {
        const bool matchA = !dynA || rbA->solverIslandId == existingId;
        const bool matchB = !dynB || rbB->solverIslandId == existingId;
        if (matchA && matchB)
        {
            if ((rbA != nullptr && rbA->getIsSleeping()) || (rbB != nullptr && rbB->getIsSleeping()))
            {
                persistentIslands[static_cast<size_t>(existingId)].hasSleepingEndpoint = true;
            }
            return;
        }
        removeContactFromPersistentIsland(pair);
    }

    int32_t targetId = -1;
    if (dynA && dynB)
    {
        const int32_t idA = rbA->solverIslandId;
        const int32_t idB = rbB->solverIslandId;
        if (idA == -1 && idB == -1)
        {
            targetId = acquirePersistentIsland();
            PersistentIsland &island = persistentIslands[static_cast<size_t>(targetId)];
            addBodyToPersistentIsland(island, rbA);
            addBodyToPersistentIsland(island, rbB);
        }
        else if (idA != -1 && idB == -1)
        {
            targetId = idA;
            addBodyToPersistentIsland(persistentIslands[static_cast<size_t>(targetId)], rbB);
        }
        else if (idA == -1 && idB != -1)
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
        if (dynBody->solverIslandId == -1)
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
    pair->solverIslandId = targetId;
    pair->solverIslandContactIndex = island.contacts.size();
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

void PhysicsManager::splitPersistentIsland(int32_t islandId)
{
    PersistentIsland &island = persistentIslands[static_cast<size_t>(islandId)];
    island.splitQueued = false;
    if (island.id == -1 || !island.maySplit)
    {
        return;
    }
    island.maySplit = false;

    const size_t bodyCount = island.bodies.size();
    if (bodyCount <= 1 || island.contacts.empty())
    {
        if (island.contacts.empty() && island.id != -1)
        {
            recyclePersistentIsland(islandId);
        }
        return;
    }

    splitAdjHead.assign(bodyCount, -1);
    splitAdjNext.clear();
    splitAdjTo.clear();
    splitAdjNext.reserve(island.contacts.size() * 2);
    splitAdjTo.reserve(island.contacts.size() * 2);

    const auto addEdge = [&](uint32_t u, uint32_t v) {
        splitAdjTo.push_back(v);
        splitAdjNext.push_back(splitAdjHead[u]);
        splitAdjHead[u] = static_cast<int32_t>(splitAdjTo.size() - 1);
    };

    for (const ColliderPair *pair : island.contacts)
    {
        auto *rbA = pair->getObjectA()->getAttachedRigidbody();
        auto *rbB = pair->getObjectB()->getAttachedRigidbody();
        if (rbA != nullptr && rbB != nullptr && rbA->solverIslandId == islandId && rbB->solverIslandId == islandId)
        {
            const uint32_t u = static_cast<uint32_t>(rbA->solverIslandBodyIndex);
            const uint32_t v = static_cast<uint32_t>(rbB->solverIslandBodyIndex);
            if (u != v)
            {
                addEdge(u, v);
                addEdge(v, u);
            }
        }
    }

    splitComponentOfBody.assign(bodyCount, -1);
    splitQueue.clear();
    int32_t componentCount = 0;

    for (size_t start = 0; start < bodyCount; ++start)
    {
        if (splitComponentOfBody[start] != -1)
        {
            continue;
        }
        const int32_t comp = componentCount++;
        splitComponentOfBody[start] = comp;
        splitQueue.clear();
        splitQueue.push_back(static_cast<uint32_t>(start));
        for (size_t head = 0; head < splitQueue.size(); ++head)
        {
            const uint32_t u = splitQueue[head];
            for (int32_t edge = splitAdjHead[u]; edge != -1; edge = splitAdjNext[static_cast<size_t>(edge)])
            {
                const uint32_t v = splitAdjTo[static_cast<size_t>(edge)];
                if (splitComponentOfBody[v] == -1)
                {
                    splitComponentOfBody[v] = comp;
                    splitQueue.push_back(v);
                }
            }
        }
    }

    if (componentCount <= 1)
    {
        return;
    }

    clearIslandColorGraph(island);

    // Separate bodies and contacts across connected components, preserving relative contact order.
    splitRemainingContacts.assign(island.contacts.begin(), island.contacts.end());
    std::vector<Rigidbody *> oldBodies;
    oldBodies.swap(island.bodies);
    island.contacts.clear();
    const bool hadSleeping = island.hasSleepingEndpoint;
    island.hasSleepingEndpoint = false;

    splitComponentIslandIds.assign(static_cast<size_t>(componentCount), -1);
    for (const ColliderPair *pair : splitRemainingContacts)
    {
        auto *rbA = pair->getObjectA()->getAttachedRigidbody();
        auto *rbB = pair->getObjectB()->getAttachedRigidbody();
        int32_t comp = -1;
        if (rbA != nullptr && rbA->solverIslandId == islandId)
        {
            comp = splitComponentOfBody[rbA->solverIslandBodyIndex];
        }
        else if (rbB != nullptr && rbB->solverIslandId == islandId)
        {
            comp = splitComponentOfBody[rbB->solverIslandBodyIndex];
        }
        if (comp >= 0 && splitComponentIslandIds[static_cast<size_t>(comp)] == -1)
        {
            splitComponentIslandIds[static_cast<size_t>(comp)] = -2; // mark component as having contacts
        }
    }

    bool reusedPrimary = false;
    for (int32_t comp = 0; comp < componentCount; ++comp)
    {
        if (splitComponentIslandIds[static_cast<size_t>(comp)] != -2)
        {
            continue;
        }
        if (!reusedPrimary)
        {
            splitComponentIslandIds[static_cast<size_t>(comp)] = islandId;
            reusedPrimary = true;
        }
        else
        {
            splitComponentIslandIds[static_cast<size_t>(comp)] = acquirePersistentIsland();
        }
    }

    // Re-home bodies (components with 0 contacts detach cleanly).
    for (size_t i = 0; i < oldBodies.size(); ++i)
    {
        Rigidbody *body = oldBodies[i];
        const int32_t comp = splitComponentOfBody[i];
        const int32_t targetId = splitComponentIslandIds[static_cast<size_t>(comp)];
        body->solverIslandId = -1;
        body->solverIslandBodyIndex = SIZE_MAX;
        if (targetId != -1)
        {
            addBodyToPersistentIsland(persistentIslands[static_cast<size_t>(targetId)], body);
        }
    }

    // Re-home contacts preserving relative order.
    for (const ColliderPair *pair : splitRemainingContacts)
    {
        auto *rbA = pair->getObjectA()->getAttachedRigidbody();
        auto *rbB = pair->getObjectB()->getAttachedRigidbody();
        int32_t targetId = -1;
        if (rbA != nullptr && rbA->solverIslandId != -1)
        {
            targetId = rbA->solverIslandId;
        }
        else if (rbB != nullptr && rbB->solverIslandId != -1)
        {
            targetId = rbB->solverIslandId;
        }
        if (targetId != -1)
        {
            PersistentIsland &target = persistentIslands[static_cast<size_t>(targetId)];
            pair->solverIslandId = targetId;
            pair->solverIslandContactIndex = target.contacts.size();
            target.contacts.push_back(pair);
            if (hadSleeping &&
                ((rbA != nullptr && rbA->getIsSleeping()) || (rbB != nullptr && rbB->getIsSleeping())))
            {
                target.hasSleepingEndpoint = true;
            }
            setPersistentIslandActive(target, true);
        }
        else
        {
            pair->solverIslandId = -1;
            pair->solverIslandContactIndex = SIZE_MAX;
        }
    }

    if (!reusedPrimary)
    {
        recyclePersistentIsland(islandId);
    }
}

void PhysicsManager::splitDirtyIslands()
{
    if (dirtySplitIslandIds.empty())
    {
        return;
    }
    for (size_t i = 0; i < dirtySplitIslandIds.size(); ++i)
    {
        splitPersistentIsland(dirtySplitIslandIds[i]);
    }
    dirtySplitIslandIds.clear();
}

void PhysicsManager::wakePersistentIslandEndpoints()
{
    const bool checkAll = !pendingBodyChanges.empty();
    for (int32_t islandId : activeIslandIds)
    {
        PersistentIsland &island = persistentIslands[static_cast<size_t>(islandId)];
        if (!island.hasSleepingEndpoint && !checkAll)
        {
            continue;
        }
        bool stillHasSleeping = false;
        for (const ColliderPair *pair : island.contacts)
        {
            auto *collision = pair->getCollision();
            if (collision == nullptr)
            {
                continue;
            }
            auto *rbA = collision->getReferenceObject()->getAttachedRigidbody();
            auto *rbB = collision->getIncidentObject()->getAttachedRigidbody();
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

