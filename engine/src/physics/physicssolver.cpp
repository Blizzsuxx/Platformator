#include "physics/physicsmanager.h"
#include "physics/physicssolver_simd.h"

#include "benchmark/benchmark.h"
#include "benchmark/workprobe.h"
#include "core/constants.h"
#include "core/gamemanager.h"
#include "core/runtimeaccess.h"
#include "oneapi/tbb.h"
#include "physics/physicsmembership.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>

namespace
{
constexpr size_t parallelSolverBatchSize = 128;
constexpr size_t parallelPreStepThreshold = 256;
constexpr uint32_t UNMAPPED_SOLVER_BODY = std::numeric_limits<uint32_t>::max();

bool hasWakeMotion(const Rigidbody *other) noexcept
{
    return other->getVelocity().squaredNorm() > LINEAR_SLEEP_THRESHOLD * LINEAR_SLEEP_THRESHOLD ||
           std::abs(other->getAngularVelocity()) > ANGULAR_SLEEP_THRESHOLD ||
           (other->getBodyType() == KINEMATIC &&
            (!other->getVelocity().isZero(0) || other->getAngularVelocity() != 0));
}

inline void applyLocalImpulse(Eigen::Vector2f &velocity, float &angularVelocity, float inverseMass,
                              float inverseMomentOfInertia, const Eigen::Vector2f &impulse,
                              const Eigen::Vector2f &arm) noexcept
{
    velocity += impulse * inverseMass;
    angularVelocity += inverseMomentOfInertia * (arm.x() * impulse.y() - arm.y() * impulse.x());
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

// ============================================================================
// Solver Entry & Fused Per-Island / Ordered-Batch Execution
// ============================================================================

void PhysicsManager::resolveCollisions(double timeDelta)
{
    PLATFORMATOR_BENCH_SCOPE(ResolveCollisions);

    const float inverseTimeDelta = timeDelta > 0.0 ? static_cast<float>(1.0 / timeDelta) : 0.0f;

    if (!kinematicSolverPairs.empty())
    {
        for (const ColliderPair *pair : kinematicSolverPairs)
        {
            auto *collision = pair->getCollision();
            if (collision == nullptr)
            {
                continue;
            }
            auto *rbA = collision->getReferenceObject()->getAttachedRigidbody();
            auto *rbB = collision->getIncidentObject()->getAttachedRigidbody();
            if (rbA != nullptr && rbB != nullptr)
            {
                resolveKinematicCollision(collision, rbA, rbB);
            }
        }
    }

    splitDirtyIslands();
    wakePersistentIslandEndpoints();

    solveConstraintIterations(inverseTimeDelta);

    reconcileChangedBodies();
    updateSleepingStates(timeDelta);

#if PLATFORMATOR_ENABLE_BENCHMARKS
    PLATFORMATOR_BENCH_SET_COUNTER(AwakeDynamicBodyCount, countAwakeDynamicBodies());
#endif
}

void PhysicsManager::solvePersistentIsland(const PersistentIsland &island, float inverseTimeDelta,
                                           IslandSolverScratch &scratch)
{
    auto &local = scratch.constraints;
    auto &bodies = scratch.bodies;
    auto &writable = scratch.writableBodies;
    local.clear();
    if (local.capacity() < island.contacts.size())
    {
        local.reserve(island.contacts.size());
    }
    bodies.clear();
    bodies.push_back(SolverBodyState{}); // slot 0: shared zero-velocity read-only body (static/sleeping)
    writable.clear();

    bool allIndexed = true;
    const auto mapBody = [&](Rigidbody *rb) -> uint32_t {
        if (rb->getBodyType() != DYNAMIC || rb->getIsSleeping())
        {
            if (rb->getBodyType() == KINEMATIC && (!rb->getVelocity().isZero(0) || rb->getAngularVelocity() != 0.0f))
            {
                const uint32_t localIdx = static_cast<uint32_t>(bodies.size());
                bodies.push_back(SolverBodyState{rb->getVelocity(), rb->getAngularVelocity(), 0.0f, 0.0f});
                return localIdx;
            }
            return 0;
        }
        const size_t regIdx = rb->getPhysicsManagerIndex();
        if (regIdx >= registeredBodies.size() || registeredBodies[regIdx] != rb)
        {
            allIndexed = false;
            return 0;
        }
        uint32_t &mapped = solverBodyLocalIndices[regIdx];
        if (mapped == UNMAPPED_SOLVER_BODY)
        {
            mapped = static_cast<uint32_t>(bodies.size());
            bodies.push_back(SolverBodyState{rb->getVelocity(), rb->getAngularVelocity(), rb->getInverseMass(),
                                             rb->getInverseMomentOfInertia()});
            writable.emplace_back(regIdx, rb);
        }
        return mapped;
    };

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
        local.emplace_back();
        SolverConstraint &c = local.back();
        prepareSolverConstraint(SolverPreStepJob{collision, rbA, rbB}, inverseTimeDelta, c);
        if (allIndexed)
        {
            c.bodyIndexA = mapBody(rbA);
            c.bodyIndexB = mapBody(rbB);
        }
    }

    if (local.empty())
    {
        for (const auto &[regIdx, rb] : writable)
        {
            (void)rb;
            solverBodyLocalIndices[regIdx] = UNMAPPED_SOLVER_BODY;
        }
        return;
    }

    if (allIndexed)
    {
        for (const SolverConstraint &constraint : local)
        {
            applyWarmStart(constraint, bodies[constraint.bodyIndexA], bodies[constraint.bodyIndexB]);
        }
        for (size_t iteration = 0; iteration < COLLISION_SOLVER_ITERATIONS; ++iteration)
        {
            for (SolverConstraint &constraint : local)
            {
                resolveCollision(constraint, bodies[constraint.bodyIndexA], bodies[constraint.bodyIndexB]);
            }
        }
        for (const SolverConstraint &constraint : local)
        {
            storeConstraintImpulses(constraint);
        }
        for (const auto &[regIdx, rb] : writable)
        {
            const uint32_t localIdx = solverBodyLocalIndices[regIdx];
            const SolverBodyState &state = bodies[localIdx];
            rb->velocity = state.velocity;
            rb->angularVelocity = state.angularVelocity;
            solverBodyLocalIndices[regIdx] = UNMAPPED_SOLVER_BODY;
        }
        return;
    }

    for (const auto &[regIdx, rb] : writable)
    {
        (void)rb;
        solverBodyLocalIndices[regIdx] = UNMAPPED_SOLVER_BODY;
    }

    // Fallback if any dynamic body in the island is not yet registered in registeredBodies.
    for (const SolverConstraint &constraint : local)
    {
        const Eigen::Vector2f &normal = constraint.normal;
        const Eigen::Vector2f &tangent = constraint.tangent;
        for (uint8_t i = 0; i < constraint.pointCount; ++i)
        {
            const Eigen::Vector2f accumulatedImpulse =
                constraint.accumulatedNormalImpulse[i] * normal + constraint.accumulatedTangentImpulse[i] * tangent;
            constraint.rbA->applySolverImpulse(-accumulatedImpulse, constraint.rA[i]);
            constraint.rbB->applySolverImpulse(accumulatedImpulse, constraint.rB[i]);
        }
    }
    for (size_t iteration = 0; iteration < COLLISION_SOLVER_ITERATIONS; ++iteration)
    {
        for (SolverConstraint &constraint : local)
        {
            const bool dynA = constraint.rbA->getBodyType() == DYNAMIC && !constraint.rbA->getIsSleeping();
            const bool dynB = constraint.rbB->getBodyType() == DYNAMIC && !constraint.rbB->getIsSleeping();
            SolverBodyState bodyA{constraint.rbA->getVelocity(), constraint.rbA->getAngularVelocity(),
                                  dynA ? constraint.rbA->getInverseMass() : 0.0f,
                                  dynA ? constraint.rbA->getInverseMomentOfInertia() : 0.0f};
            SolverBodyState bodyB{constraint.rbB->getVelocity(), constraint.rbB->getAngularVelocity(),
                                  dynB ? constraint.rbB->getInverseMass() : 0.0f,
                                  dynB ? constraint.rbB->getInverseMomentOfInertia() : 0.0f};
            resolveCollision(constraint, bodyA, bodyB);
            if (dynA)
            {
                constraint.rbA->velocity = bodyA.velocity;
                constraint.rbA->angularVelocity = bodyA.angularVelocity;
            }
            if (dynB)
            {
                constraint.rbB->velocity = bodyB.velocity;
                constraint.rbB->angularVelocity = bodyB.angularVelocity;
            }
        }
    }
    for (const SolverConstraint &constraint : local)
    {
        storeConstraintImpulses(constraint);
    }
}

bool PhysicsManager::solvePersistentIslandGroup8(const int32_t *islandIds, size_t groupSize, float inverseTimeDelta,
                                                 IslandSolverScratch &scratch)
{
#if PLATFORMATOR_HAS_RUNTIME_AVX2
    using namespace platformator_solver_simd;
    auto &wideBodies = scratch.wideBodies;
    auto &writable = scratch.writableBodies;

    size_t maxPossibleSteps = 0;
    for (size_t lane = 0; lane < groupSize; ++lane)
    {
        maxPossibleSteps =
            std::max(maxPossibleSteps, persistentIslands[static_cast<size_t>(islandIds[lane])].contacts.size());
    }
    if (maxPossibleSteps == 0)
    {
        return true;
    }

    wideBodies.clear();
    wideBodies.push_back(WideBodyVel{}); // slot 0: shared static/sleeping zero-velocity body
    writable.clear();

    bool allIndexed = true;
    const auto mapBody = [&](Rigidbody *rb, float &outInvM, float &outInvI) -> uint32_t {
        if (rb->getBodyType() != DYNAMIC || rb->getIsSleeping())
        {
            outInvM = 0.0f;
            outInvI = 0.0f;
            if (rb->getBodyType() == KINEMATIC && (!rb->getVelocity().isZero(0) || rb->getAngularVelocity() != 0.0f))
            {
                const uint32_t localIdx = static_cast<uint32_t>(wideBodies.size());
                const auto &v = rb->getVelocity();
                wideBodies.push_back(WideBodyVel{v.x(), v.y(), rb->getAngularVelocity(), 0.0f});
                return localIdx;
            }
            return 0;
        }
        const size_t regIdx = rb->getPhysicsManagerIndex();
        if (regIdx >= registeredBodies.size() || registeredBodies[regIdx] != rb)
        {
            allIndexed = false;
            outInvM = 0.0f;
            outInvI = 0.0f;
            return 0;
        }
        outInvM = rb->getInverseMass();
        outInvI = rb->getInverseMomentOfInertia();
        uint32_t &mapped = solverBodyLocalIndices[regIdx];
        if (mapped == UNMAPPED_SOLVER_BODY)
        {
            mapped = static_cast<uint32_t>(wideBodies.size());
            const auto &v = rb->getVelocity();
            wideBodies.push_back(WideBodyVel{v.x(), v.y(), rb->getAngularVelocity(), 0.0f});
            writable.emplace_back(regIdx, rb);
        }
        return mapped;
    };

    thread_local std::vector<WideConstraint8> wideSteps;
    wideSteps.resize(maxPossibleSteps);
    for (size_t step = 0; step < maxPossibleSteps; ++step)
    {
        wideSteps[step].blockMask = 0;
        wideSteps[step].twoPointMask = 0;
        wideSteps[step].writeMaskA = 0;
        wideSteps[step].writeMaskB = 0;
    }

    size_t laneCounts[8] = {};
    size_t maxLaneCount = 0;
    SolverConstraint tmpConstraint;
    for (size_t lane = 0; lane < groupSize; ++lane)
    {
        const PersistentIsland &island = persistentIslands[static_cast<size_t>(islandIds[lane])];
        size_t step = 0;
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
            prepareSolverConstraint(SolverPreStepJob{collision, rbA, rbB}, inverseTimeDelta, tmpConstraint);
            float invMA = 0.0f, invIA = 0.0f, invMB = 0.0f, invIB = 0.0f;
            tmpConstraint.bodyIndexA = mapBody(rbA, invMA, invIA);
            tmpConstraint.bodyIndexB = mapBody(rbB, invMB, invIB);
            if (!allIndexed)
            {
                break;
            }
            packWideLane(wideSteps[step], lane, &tmpConstraint, invMA, invIA, invMB, invIB);
            ++step;
        }
        if (!allIndexed)
        {
            break;
        }
        laneCounts[lane] = step;
        maxLaneCount = std::max(maxLaneCount, step);
    }

    if (!allIndexed)
    {
        for (const auto &[regIdx, rb] : writable)
        {
            (void)rb;
            solverBodyLocalIndices[regIdx] = UNMAPPED_SOLVER_BODY;
        }
        return false;
    }

    if (maxLaneCount == 0)
    {
        for (const auto &[regIdx, rb] : writable)
        {
            (void)rb;
            solverBodyLocalIndices[regIdx] = UNMAPPED_SOLVER_BODY;
        }
        return true;
    }

    // Zero any trailing/unused lanes in [0, maxLaneCount).
    for (size_t lane = 0; lane < 8; ++lane)
    {
        const size_t startStep = lane < groupSize ? laneCounts[lane] : 0;
        for (size_t step = startStep; step < maxLaneCount; ++step)
        {
            packWideLane<SolverConstraint>(wideSteps[step], lane, nullptr);
        }
    }

    WideBodyVel *bodyData = wideBodies.data();
    for (size_t step = 0; step < maxLaneCount; ++step)
    {
        applyWarmStartWide8AVX2(wideSteps[step], bodyData);
    }
    for (size_t iteration = 0; iteration < COLLISION_SOLVER_ITERATIONS; ++iteration)
    {
        for (size_t step = 0; step < maxLaneCount; ++step)
        {
            resolveWide8AVX2(wideSteps[step], bodyData);
        }
    }
    for (size_t step = 0; step < maxLaneCount; ++step)
    {
        storeWideImpulses(wideSteps[step]);
    }

    for (const auto &[regIdx, rb] : writable)
    {
        const uint32_t localIdx = solverBodyLocalIndices[regIdx];
        const WideBodyVel &state = wideBodies[localIdx];
        rb->velocity.x() = state.vx;
        rb->velocity.y() = state.vy;
        rb->angularVelocity = state.w;
        solverBodyLocalIndices[regIdx] = UNMAPPED_SOLVER_BODY;
    }
    return true;
#else
    (void)islandIds;
    (void)groupSize;
    (void)inverseTimeDelta;
    (void)scratch;
    return false;
#endif
}

void PhysicsManager::buildIslandWorkUnits()
{
    const size_t n = activeIslandIds.size();
    bucketedIslandIds.resize(n);
    islandWorkUnits.clear();
    if (n == 0)
    {
        return;
    }

    // O(N) stable counting sort by contact count (clamped to 128) so 8-wide SIMD island groups
    // have nearly identical lengths while preserving exact deterministic order within each bucket.
    constexpr size_t MAX_BUCKET = 128;
    uint32_t counts[MAX_BUCKET + 1] = {};
    for (int32_t islandId : activeIslandIds)
    {
        const size_t c = std::min<size_t>(persistentIslands[static_cast<size_t>(islandId)].contacts.size(), MAX_BUCKET);
        ++counts[c];
    }
    uint32_t offsets[MAX_BUCKET + 1];
    uint32_t running = 0;
    for (size_t b = 0; b <= MAX_BUCKET; ++b)
    {
        offsets[b] = running;
        running += counts[b];
    }
    for (int32_t islandId : activeIslandIds)
    {
        const size_t c = std::min<size_t>(persistentIslands[static_cast<size_t>(islandId)].contacts.size(), MAX_BUCKET);
        bucketedIslandIds[offsets[c]++] = islandId;
    }

    const bool useAVX2 = platformator_solver_simd::cpuSupportsAVX2();
    islandWorkUnits.reserve(n);
    size_t idx = 0;
    while (idx < n)
    {
        const size_t remaining = n - idx;
        if (useAVX2 && remaining >= 4)
        {
            const size_t groupSize = std::min<size_t>(8, remaining);
            size_t minCount = SIZE_MAX;
            size_t maxCount = 0;
            size_t sumCount = 0;
            for (size_t k = 0; k < groupSize; ++k)
            {
                const size_t c = persistentIslands[static_cast<size_t>(bucketedIslandIds[idx + k])].contacts.size();
                minCount = std::min(minCount, c);
                maxCount = std::max(maxCount, c);
                sumCount += c;
            }
            if (minCount >= 2 && maxCount <= minCount * 2 + 12)
            {
                islandWorkUnits.push_back(IslandWorkUnit{static_cast<uint32_t>(idx), static_cast<uint8_t>(groupSize),
                                                         static_cast<uint32_t>(sumCount)});
                idx += groupSize;
                continue;
            }
        }
        const size_t c = persistentIslands[static_cast<size_t>(bucketedIslandIds[idx])].contacts.size();
        islandWorkUnits.push_back(
            IslandWorkUnit{static_cast<uint32_t>(idx), 1, static_cast<uint32_t>(c)});
        ++idx;
    }
}

void PhysicsManager::executeIslandWorkUnits(size_t beginUnit, size_t endUnit, float inverseTimeDelta,
                                            IslandSolverScratch &scratch)
{
    for (size_t u = beginUnit; u < endUnit; ++u)
    {
        const IslandWorkUnit &unit = islandWorkUnits[u];
        if (unit.count > 1)
        {
            if (solvePersistentIslandGroup8(bucketedIslandIds.data() + unit.begin, unit.count, inverseTimeDelta,
                                            scratch))
            {
                continue;
            }
            for (uint8_t k = 0; k < unit.count; ++k)
            {
                solvePersistentIsland(persistentIslands[static_cast<size_t>(bucketedIslandIds[unit.begin + k])],
                                      inverseTimeDelta, scratch);
            }
            continue;
        }
        solvePersistentIsland(persistentIslands[static_cast<size_t>(bucketedIslandIds[unit.begin])], inverseTimeDelta,
                              scratch);
    }
}

bool PhysicsManager::solveColoredIsland(PersistentIsland &island, float inverseTimeDelta, bool allowParallel)
{
    ensureIslandColorGraph(island);
    const IslandColorGraph &graph = *island.colorGraph;

    solverBodyStates.clear();
    solverBodyStates.push_back(SolverBodyState{}); // slot 0: shared static/sleeping zero-velocity body
    solverTouchedWritableBodies.clear();

    const bool useAVX2 = platformator_solver_simd::cpuSupportsAVX2();
    if (useAVX2)
    {
        solverWideBodyVels.clear();
        solverWideBodyVels.push_back(WideBodyVel{});
    }

    for (Rigidbody *rb : island.bodies)
    {
        if (rb->getBodyType() != DYNAMIC || rb->getIsSleeping())
        {
            continue;
        }
        const size_t regIdx = rb->getPhysicsManagerIndex();
        if (regIdx >= registeredBodies.size() || registeredBodies[regIdx] != rb)
        {
            for (const auto &[mappedRegIdx, mappedRb] : solverTouchedWritableBodies)
            {
                (void)mappedRb;
                solverBodyLocalIndices[mappedRegIdx] = UNMAPPED_SOLVER_BODY;
            }
            return false;
        }
        uint32_t &mapped = solverBodyLocalIndices[regIdx];
        if (mapped == UNMAPPED_SOLVER_BODY)
        {
            mapped = static_cast<uint32_t>(solverBodyStates.size());
            const auto &v = rb->getVelocity();
            const float w = rb->getAngularVelocity();
            solverBodyStates.push_back(
                SolverBodyState{v, w, rb->getInverseMass(), rb->getInverseMomentOfInertia()});
            if (useAVX2)
            {
                solverWideBodyVels.push_back(WideBodyVel{v.x(), v.y(), w, 0.0f});
            }
            solverTouchedWritableBodies.emplace_back(regIdx, rb);
        }
    }

    const size_t expectedContacts = island.contacts.size();
    solverPreStepJobs.clear();
    solverEndpoints.clear();
    if (solverPreStepJobs.capacity() < expectedContacts)
    {
        solverPreStepJobs.reserve(expectedContacts);
    }
    if (solverEndpoints.capacity() < expectedContacts)
    {
        solverEndpoints.reserve(expectedContacts);
    }

    const bool hasMovingKinematic = !movingKinematicBodies.empty();
    const auto resolveBodySlot = [&](Rigidbody *rb) -> uint32_t {
        if (rb->getBodyType() == DYNAMIC && !rb->getIsSleeping())
        {
            const size_t regIdx = rb->getPhysicsManagerIndex();
            if (regIdx < registeredBodies.size() && registeredBodies[regIdx] == rb)
            {
                const uint32_t slot = solverBodyLocalIndices[regIdx];
                if (slot != UNMAPPED_SOLVER_BODY)
                {
                    return slot;
                }
            }
            return UNMAPPED_SOLVER_BODY;
        }
        if (hasMovingKinematic && rb->getBodyType() == KINEMATIC &&
            (!rb->getVelocity().isZero(0) || rb->getAngularVelocity() != 0.0f))
        {
            const uint32_t slot = static_cast<uint32_t>(solverBodyStates.size());
            const auto &v = rb->getVelocity();
            const float w = rb->getAngularVelocity();
            solverBodyStates.push_back(SolverBodyState{v, w, 0.0f, 0.0f});
            if (useAVX2)
            {
                solverWideBodyVels.push_back(WideBodyVel{v.x(), v.y(), w, 0.0f});
            }
            return slot;
        }
        return 0;
    };

    std::array<size_t, IslandColorGraph::TOTAL_BUCKETS> colorBegin{};
    std::array<size_t, IslandColorGraph::TOTAL_BUCKETS> colorEnd{};
    bool allMapped = true;
    for (size_t c = 0; c < IslandColorGraph::TOTAL_BUCKETS; ++c)
    {
        colorBegin[c] = solverPreStepJobs.size();
        for (const ColliderPair *pair : graph.buckets[c])
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
            const uint32_t idxA = resolveBodySlot(rbA);
            const uint32_t idxB = resolveBodySlot(rbB);
            if (idxA == UNMAPPED_SOLVER_BODY || idxB == UNMAPPED_SOLVER_BODY)
            {
                allMapped = false;
                break;
            }
            solverPreStepJobs.push_back(SolverPreStepJob{collision, rbA, rbB});
            solverEndpoints.emplace_back(idxA, idxB);
        }
        colorEnd[c] = solverPreStepJobs.size();
        if (!allMapped)
        {
            break;
        }
    }

    if (!allMapped)
    {
        for (const auto &[regIdx, rb] : solverTouchedWritableBodies)
        {
            (void)rb;
            solverBodyLocalIndices[regIdx] = UNMAPPED_SOLVER_BODY;
        }
        return false;
    }

    const size_t constraintCount = solverPreStepJobs.size();
    if (constraintCount == 0)
    {
        for (const auto &[regIdx, rb] : solverTouchedWritableBodies)
        {
            (void)rb;
            solverBodyLocalIndices[regIdx] = UNMAPPED_SOLVER_BODY;
        }
        return true;
    }

    solverConstraints.resize(constraintCount);
    tbb::task_group_context context(tbb::task_group_context::isolated);
    if (!allowParallel || constraintCount < parallelPreStepThreshold)
    {
        for (size_t i = 0; i < constraintCount; ++i)
        {
            prepareSolverConstraint(solverPreStepJobs[i], inverseTimeDelta, solverConstraints[i]);
            solverConstraints[i].bodyIndexA = static_cast<uint32_t>(solverEndpoints[i].first);
            solverConstraints[i].bodyIndexB = static_cast<uint32_t>(solverEndpoints[i].second);
        }
    }
    else
    {
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, constraintCount, 64),
            [&](const auto &range) {
                for (size_t i = range.begin(); i != range.end(); ++i)
                {
                    prepareSolverConstraint(solverPreStepJobs[i], inverseTimeDelta, solverConstraints[i]);
                    solverConstraints[i].bodyIndexA = static_cast<uint32_t>(solverEndpoints[i].first);
                    solverConstraints[i].bodyIndexB = static_cast<uint32_t>(solverEndpoints[i].second);
                }
            },
            tbb::static_partitioner(), context);
    }

#if PLATFORMATOR_HAS_RUNTIME_AVX2
    if (useAVX2)
    {
        using namespace platformator_solver_simd;
        thread_local std::array<std::vector<WideConstraint8>, IslandColorGraph::MAX_PARALLEL_COLORS> colorWideBlocks;
        thread_local std::array<std::vector<uint32_t>, IslandColorGraph::MAX_PARALLEL_COLORS> colorTails;
        thread_local std::array<std::vector<uint32_t>, IslandColorGraph::MAX_PARALLEL_COLORS> colorBlockBuf;
        thread_local std::array<std::vector<uint32_t>, IslandColorGraph::MAX_PARALLEL_COLORS> colorSingleBuf;
        auto *wideBuckets = colorWideBlocks.data();
        auto *tailBuckets = colorTails.data();
        auto *blockBuffers = colorBlockBuf.data();
        auto *singleBuffers = colorSingleBuf.data();

        std::array<uint8_t, IslandColorGraph::MAX_PARALLEL_COLORS> activeColors{};
        size_t activeColorCount = 0;
        for (uint8_t c = 0; c < IslandColorGraph::MAX_PARALLEL_COLORS; ++c)
        {
            if (colorEnd[c] > colorBegin[c])
            {
                activeColors[activeColorCount++] = c;
            }
        }

        const auto packColor = [&, wideBuckets, tailBuckets, blockBuffers, singleBuffers](uint8_t c) {
            auto &wideOut = wideBuckets[c];
            auto &tailOut = tailBuckets[c];
            auto &blockBuf = blockBuffers[c];
            auto &singleBuf = singleBuffers[c];
            wideOut.clear();
            tailOut.clear();
            const size_t begin = colorBegin[c];
            const size_t end = colorEnd[c];
            if (end - begin < 4)
            {
                for (size_t idx = begin; idx < end; ++idx)
                {
                    tailOut.push_back(static_cast<uint32_t>(idx));
                }
                return;
            }
            blockBuf.clear();
            singleBuf.clear();
            for (size_t idx = begin; idx < end; ++idx)
            {
                const SolverConstraint &sc = solverConstraints[idx];
                if (sc.blockSolve)
                {
                    blockBuf.push_back(static_cast<uint32_t>(idx));
                }
                else if (sc.pointCount == 1)
                {
                    singleBuf.push_back(static_cast<uint32_t>(idx));
                }
                else
                {
                    tailOut.push_back(static_cast<uint32_t>(idx));
                }
            }
            const auto emitBucket = [&](const std::vector<uint32_t> &bucket) {
                size_t pos = 0;
                while (pos + 8 <= bucket.size())
                {
                    wideOut.emplace_back();
                    WideConstraint8 &wide = wideOut.back();
                    wide.blockMask = 0;
                    wide.twoPointMask = 0;
                    wide.writeMaskA = 0;
                    wide.writeMaskB = 0;
                    for (size_t lane = 0; lane < 8; ++lane)
                    {
                        const SolverConstraint &sc = solverConstraints[bucket[pos + lane]];
                        const SolverBodyState &bA = solverBodyStates[sc.bodyIndexA];
                        const SolverBodyState &bB = solverBodyStates[sc.bodyIndexB];
                        packWideLane(wide, lane, &sc, bA.inverseMass, bA.inverseMomentOfInertia, bB.inverseMass,
                                     bB.inverseMomentOfInertia);
                    }
                    pos += 8;
                }
                if (bucket.size() - pos >= 4)
                {
                    wideOut.emplace_back();
                    WideConstraint8 &wide = wideOut.back();
                    wide.blockMask = 0;
                    wide.twoPointMask = 0;
                    wide.writeMaskA = 0;
                    wide.writeMaskB = 0;
                    const size_t rem = bucket.size() - pos;
                    for (size_t lane = 0; lane < 8; ++lane)
                    {
                        if (lane < rem)
                        {
                            const SolverConstraint &sc = solverConstraints[bucket[pos + lane]];
                            const SolverBodyState &bA = solverBodyStates[sc.bodyIndexA];
                            const SolverBodyState &bB = solverBodyStates[sc.bodyIndexB];
                            packWideLane(wide, lane, &sc, bA.inverseMass, bA.inverseMomentOfInertia, bB.inverseMass,
                                         bB.inverseMomentOfInertia);
                        }
                        else
                        {
                            packWideLane<SolverConstraint>(wide, lane, nullptr);
                        }
                    }
                    pos = bucket.size();
                }
                while (pos < bucket.size())
                {
                    tailOut.push_back(bucket[pos++]);
                }
            };
            emitBucket(blockBuf);
            emitBucket(singleBuf);
        };

        if (allowParallel && activeColorCount >= 2)
        {
            tbb::parallel_for(
                size_t(0), activeColorCount, [&](size_t ac) { packColor(activeColors[ac]); }, context);
        }
        else
        {
            for (size_t ac = 0; ac < activeColorCount; ++ac)
            {
                packColor(activeColors[ac]);
            }
        }

        WideBodyVel *bodyData = solverWideBodyVels.data();
        const size_t overflowBegin = colorBegin[IslandColorGraph::OVERFLOW_COLOR];
        const size_t overflowEnd = colorEnd[IslandColorGraph::OVERFLOW_COLOR];
        constexpr size_t parallelWideThreshold = 8;

        const auto runTailWarmStart = [&](const SolverConstraint &sc) {
            WideBodyVel &vA = bodyData[sc.bodyIndexA];
            WideBodyVel &vB = bodyData[sc.bodyIndexB];
            SolverBodyState sA{{vA.vx, vA.vy}, vA.w, solverBodyStates[sc.bodyIndexA].inverseMass,
                               solverBodyStates[sc.bodyIndexA].inverseMomentOfInertia};
            SolverBodyState sB{{vB.vx, vB.vy}, vB.w, solverBodyStates[sc.bodyIndexB].inverseMass,
                               solverBodyStates[sc.bodyIndexB].inverseMomentOfInertia};
            applyWarmStart(sc, sA, sB);
            vA.vx = sA.velocity.x();
            vA.vy = sA.velocity.y();
            vA.w = sA.angularVelocity;
            vB.vx = sB.velocity.x();
            vB.vy = sB.velocity.y();
            vB.w = sB.angularVelocity;
        };

        const auto runTailResolve = [&](SolverConstraint &sc) {
            WideBodyVel &vA = bodyData[sc.bodyIndexA];
            WideBodyVel &vB = bodyData[sc.bodyIndexB];
            SolverBodyState sA{{vA.vx, vA.vy}, vA.w, solverBodyStates[sc.bodyIndexA].inverseMass,
                               solverBodyStates[sc.bodyIndexA].inverseMomentOfInertia};
            SolverBodyState sB{{vB.vx, vB.vy}, vB.w, solverBodyStates[sc.bodyIndexB].inverseMass,
                               solverBodyStates[sc.bodyIndexB].inverseMomentOfInertia};
            resolveCollision(sc, sA, sB);
            vA.vx = sA.velocity.x();
            vA.vy = sA.velocity.y();
            vA.w = sA.angularVelocity;
            vB.vx = sB.velocity.x();
            vB.vy = sB.velocity.y();
            vB.w = sB.angularVelocity;
        };

        for (size_t ac = 0; ac < activeColorCount; ++ac)
        {
            const uint8_t c = activeColors[ac];
            WideConstraint8 *wideData = colorWideBlocks[c].data();
            const size_t wideCount = colorWideBlocks[c].size();
            if (!allowParallel || wideCount < parallelWideThreshold)
            {
                for (size_t w = 0; w < wideCount; ++w)
                {
                    applyWarmStartWide8AVX2(wideData[w], bodyData);
                }
            }
            else
            {
                tbb::parallel_for(
                    tbb::blocked_range<size_t>(0, wideCount, 4),
                    [=](const auto &range) {
                        for (size_t w = range.begin(); w != range.end(); ++w)
                        {
                            applyWarmStartWide8AVX2(wideData[w], bodyData);
                        }
                    },
                    tbb::static_partitioner(), context);
            }
            for (uint32_t idx : colorTails[c])
            {
                runTailWarmStart(solverConstraints[idx]);
            }
        }
        for (size_t idx = overflowBegin; idx < overflowEnd; ++idx)
        {
            runTailWarmStart(solverConstraints[idx]);
        }

        for (size_t iteration = 0; iteration < COLLISION_SOLVER_ITERATIONS; ++iteration)
        {
            for (size_t ac = 0; ac < activeColorCount; ++ac)
            {
                const uint8_t c = activeColors[ac];
                WideConstraint8 *wideData = colorWideBlocks[c].data();
                const size_t wideCount = colorWideBlocks[c].size();
                if (!allowParallel || wideCount < parallelWideThreshold)
                {
                    for (size_t w = 0; w < wideCount; ++w)
                    {
                        resolveWide8AVX2(wideData[w], bodyData);
                    }
                }
                else
                {
                    tbb::parallel_for(
                        tbb::blocked_range<size_t>(0, wideCount, 4),
                        [=](const auto &range) {
                            for (size_t w = range.begin(); w != range.end(); ++w)
                            {
                                resolveWide8AVX2(wideData[w], bodyData);
                            }
                        },
                        tbb::static_partitioner(), context);
                }
                for (uint32_t idx : colorTails[c])
                {
                    runTailResolve(solverConstraints[idx]);
                }
            }
            for (size_t idx = overflowBegin; idx < overflowEnd; ++idx)
            {
                runTailResolve(solverConstraints[idx]);
            }
        }

        for (size_t ac = 0; ac < activeColorCount; ++ac)
        {
            const uint8_t c = activeColors[ac];
            for (const WideConstraint8 &wide : colorWideBlocks[c])
            {
                storeWideImpulses(wide);
            }
            for (uint32_t idx : colorTails[c])
            {
                storeConstraintImpulses(solverConstraints[idx]);
            }
        }
        for (size_t idx = overflowBegin; idx < overflowEnd; ++idx)
        {
            storeConstraintImpulses(solverConstraints[idx]);
        }
        for (const auto &[regIdx, rb] : solverTouchedWritableBodies)
        {
            const uint32_t localIdx = solverBodyLocalIndices[regIdx];
            const WideBodyVel &state = solverWideBodyVels[localIdx];
            rb->velocity.x() = state.vx;
            rb->velocity.y() = state.vy;
            rb->angularVelocity = state.w;
            solverBodyLocalIndices[regIdx] = UNMAPPED_SOLVER_BODY;
        }
        return true;
    }
#endif

    // Non-AVX2 scalar colored fallback (same deterministic color order).
    const size_t overflowBegin = colorBegin[IslandColorGraph::OVERFLOW_COLOR];
    const size_t overflowEnd = colorEnd[IslandColorGraph::OVERFLOW_COLOR];
    for (uint8_t c = 0; c < IslandColorGraph::MAX_PARALLEL_COLORS; ++c)
    {
        const size_t begin = colorBegin[c];
        const size_t end = colorEnd[c];
        if (!allowParallel || end - begin < parallelSolverBatchSize)
        {
            for (size_t i = begin; i < end; ++i)
            {
                const SolverConstraint &sc = solverConstraints[i];
                applyWarmStart(sc, solverBodyStates[sc.bodyIndexA], solverBodyStates[sc.bodyIndexB]);
            }
        }
        else
        {
            tbb::parallel_for(
                tbb::blocked_range<size_t>(begin, end, 32),
                [&](const auto &range) {
                    for (size_t i = range.begin(); i != range.end(); ++i)
                    {
                        const SolverConstraint &sc = solverConstraints[i];
                        applyWarmStart(sc, solverBodyStates[sc.bodyIndexA], solverBodyStates[sc.bodyIndexB]);
                    }
                },
                tbb::static_partitioner(), context);
        }
    }
    for (size_t i = overflowBegin; i < overflowEnd; ++i)
    {
        const SolverConstraint &sc = solverConstraints[i];
        applyWarmStart(sc, solverBodyStates[sc.bodyIndexA], solverBodyStates[sc.bodyIndexB]);
    }
    for (size_t iteration = 0; iteration < COLLISION_SOLVER_ITERATIONS; ++iteration)
    {
        for (uint8_t c = 0; c < IslandColorGraph::MAX_PARALLEL_COLORS; ++c)
        {
            const size_t begin = colorBegin[c];
            const size_t end = colorEnd[c];
            if (!allowParallel || end - begin < parallelSolverBatchSize)
            {
                for (size_t i = begin; i < end; ++i)
                {
                    SolverConstraint &sc = solverConstraints[i];
                    resolveCollision(sc, solverBodyStates[sc.bodyIndexA], solverBodyStates[sc.bodyIndexB]);
                }
            }
            else
            {
                tbb::parallel_for(
                    tbb::blocked_range<size_t>(begin, end, 32),
                    [&](const auto &range) {
                        for (size_t i = range.begin(); i != range.end(); ++i)
                        {
                            SolverConstraint &sc = solverConstraints[i];
                            resolveCollision(sc, solverBodyStates[sc.bodyIndexA], solverBodyStates[sc.bodyIndexB]);
                        }
                    },
                    tbb::static_partitioner(), context);
            }
        }
        for (size_t i = overflowBegin; i < overflowEnd; ++i)
        {
            SolverConstraint &sc = solverConstraints[i];
            resolveCollision(sc, solverBodyStates[sc.bodyIndexA], solverBodyStates[sc.bodyIndexB]);
        }
    }
    for (size_t i = 0; i < constraintCount; ++i)
    {
        storeConstraintImpulses(solverConstraints[i]);
    }
    for (const auto &[regIdx, rb] : solverTouchedWritableBodies)
    {
        const uint32_t localIdx = solverBodyLocalIndices[regIdx];
        const SolverBodyState &state = solverBodyStates[localIdx];
        rb->velocity = state.velocity;
        rb->angularVelocity = state.angularVelocity;
        solverBodyLocalIndices[regIdx] = UNMAPPED_SOLVER_BODY;
    }
    return true;
}

void PhysicsManager::solveConstraintIterations(float inverseTimeDelta)
{
    if (activeIslandIds.empty())
    {
        return;
    }

    if (solverBodyLocalIndices.size() < registeredBodies.size())
    {
        solverBodyLocalIndices.resize(registeredBodies.size(), UNMAPPED_SOLVER_BODY);
    }

    size_t totalContacts = 0;
    size_t largestIslandContacts = 0;
    for (int32_t islandId : activeIslandIds)
    {
        const size_t count = persistentIslands[static_cast<size_t>(islandId)].contacts.size();
        totalContacts += count;
        largestIslandContacts = std::max(largestIslandContacts, count);
    }

    constexpr size_t parallelConstraintThreshold = 256;
    constexpr size_t orderedConstraintThreshold = 256;
    const bool allowParallel = totalContacts >= parallelConstraintThreshold &&
                               tbb::global_control::active_value(tbb::global_control::max_allowed_parallelism) > 1 &&
                               tbb::this_task_arena::max_concurrency() > 1;

    if (largestIslandContacts >= orderedConstraintThreshold && largestIslandContacts > totalContacts / 2)
    {
        bool anyColored = false;
        for (int32_t islandId : activeIslandIds)
        {
            PersistentIsland &island = persistentIslands[static_cast<size_t>(islandId)];
            if (island.contacts.size() >= orderedConstraintThreshold &&
                solveColoredIsland(island, inverseTimeDelta, allowParallel))
            {
                anyColored = true;
                continue;
            }
            solvePersistentIsland(island, inverseTimeDelta, serialSolverScratch);
        }
        if (anyColored && allowParallel)
        {
            PLATFORMATOR_WORK_ADD(solverOrderedBatchDispatches, 1);
        }
        return;
    }

    if (allowParallel)
    {
        solveIslands(inverseTimeDelta, totalContacts);
        return;
    }

    solveConstraintsSerial(inverseTimeDelta);
}

void PhysicsManager::solveConstraintsSerial(float inverseTimeDelta)
{
    buildIslandWorkUnits();
    executeIslandWorkUnits(0, islandWorkUnits.size(), inverseTimeDelta, serialSolverScratch);
}

void PhysicsManager::solveIslands(float inverseTimeDelta, size_t totalContacts)
{
    buildIslandWorkUnits();
    const auto workers = std::min(tbb::global_control::active_value(tbb::global_control::max_allowed_parallelism),
                                  static_cast<size_t>(tbb::this_task_arena::max_concurrency()));
    const size_t target = std::max(size_t{1}, totalContacts / workers / 4);
    solverIslandJobEnds.clear();
    size_t jobSize = 0;
    const size_t unitCount = islandWorkUnits.size();
    for (size_t i = 0; i < unitCount; ++i)
    {
        const size_t count = islandWorkUnits[i].contactWeight;
        if (jobSize != 0 && jobSize + count > target)
        {
            solverIslandJobEnds.push_back(i);
            jobSize = 0;
        }
        jobSize += count;
        PLATFORMATOR_WORK_SET(solverLargestIslandJob,
                              std::max(platformator_test::work.solverLargestIslandJob, jobSize));
    }
    solverIslandJobEnds.push_back(unitCount);
    PLATFORMATOR_WORK_ADD(solverIslandJobs, solverIslandJobEnds.size());

    // A simulation step must finish applying impulses even if the caller's task group is cancelled.
    tbb::task_group_context context(tbb::task_group_context::isolated);
    tbb::parallel_for(
        size_t{0}, solverIslandJobEnds.size(),
        [&](size_t job) {
            auto &scratch = solverIslandScratch.local();
            const size_t begin = job == 0 ? 0 : solverIslandJobEnds[job - 1];
            const size_t end = solverIslandJobEnds[job];
            executeIslandWorkUnits(begin, end, inverseTimeDelta, scratch);
        },
        context);
}

bool PhysicsManager::prepareOrderedSolverBatches()
{
    if (sparseSolverBodies)
    {
        solverBodyNextBatch.resize(registeredBodies.size());
        for (const auto &[a, b] : solverEndpoints)
        {
            if (a != SIZE_MAX)
                solverBodyNextBatch[a] = 0;
            if (b != SIZE_MAX)
                solverBodyNextBatch[b] = 0;
        }
    }
    else
    {
        solverBodyNextBatch.assign(registeredBodies.size(), 0);
    }
    solverBatchCount = 0;
    for (size_t i = 0; i < solverEndpoints.size(); ++i)
    {
        const auto [a, b] = solverEndpoints[i];
        // Each writable body must see its incident constraints in their original order.
        const size_t batch =
            std::max(a != SIZE_MAX ? solverBodyNextBatch[a] : 0, b != SIZE_MAX ? solverBodyNextBatch[b] : 0);
        if (a != SIZE_MAX)
            solverBodyNextBatch[a] = batch + 1;
        if (b != SIZE_MAX)
            solverBodyNextBatch[b] = batch + 1;
        if (batch == solverBatchCount)
        {
            if (batch == solverBatches.size())
                solverBatches.emplace_back();
            solverBatches[batch].clear();
            ++solverBatchCount;
        }
        solverBatches[batch].push_back(i);
    }

    size_t parallelConstraints = 0;
    for (size_t batch = 0; batch < solverBatchCount; ++batch)
        if (solverBatches[batch].size() >= parallelSolverBatchSize)
            parallelConstraints += solverBatches[batch].size();
    return parallelConstraints >= solverEndpoints.size() / 2;
}

void PhysicsManager::solveOrderedBatches(float inverseTimeDelta)
{
    PLATFORMATOR_WORK_ADD(solverOrderedBatchDispatches, 1);

    const size_t constraintCount = solverPreStepJobs.size();
    solverConstraints.resize(constraintCount);

    tbb::task_group_context context(tbb::task_group_context::isolated);
    if (constraintCount < parallelPreStepThreshold)
    {
        for (size_t i = 0; i < constraintCount; ++i)
        {
            prepareSolverConstraint(solverPreStepJobs[i], inverseTimeDelta, solverConstraints[i]);
        }
    }
    else
    {
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, constraintCount, 64),
            [&](const auto &range) {
                for (size_t i = range.begin(); i != range.end(); ++i)
                {
                    prepareSolverConstraint(solverPreStepJobs[i], inverseTimeDelta, solverConstraints[i]);
                }
            },
            context);
    }

    solverBodyStates.clear();
    solverBodyStates.push_back(SolverBodyState{}); // slot 0: shared zero-velocity read-only body (never written)
    solverTouchedWritableBodies.clear();

    const auto mapBatchBody = [&](size_t regIdx, Rigidbody *rb) -> uint32_t {
        if (regIdx == SIZE_MAX)
        {
            if (rb->getBodyType() == KINEMATIC && (!rb->getVelocity().isZero(0) || rb->getAngularVelocity() != 0.0f))
            {
                const uint32_t localIdx = static_cast<uint32_t>(solverBodyStates.size());
                solverBodyStates.push_back(SolverBodyState{rb->getVelocity(), rb->getAngularVelocity(), 0.0f, 0.0f});
                return localIdx;
            }
            return 0;
        }
        uint32_t &mapped = solverBodyLocalIndices[regIdx];
        if (mapped == UNMAPPED_SOLVER_BODY)
        {
            mapped = static_cast<uint32_t>(solverBodyStates.size());
            solverBodyStates.push_back(SolverBodyState{rb->getVelocity(), rb->getAngularVelocity(),
                                                       rb->getInverseMass(), rb->getInverseMomentOfInertia()});
            solverTouchedWritableBodies.emplace_back(regIdx, rb);
        }
        return mapped;
    };

    for (size_t i = 0; i < constraintCount; ++i)
    {
        const auto [regA, regB] = solverEndpoints[i];
        solverConstraints[i].bodyIndexA = mapBatchBody(regA, solverConstraints[i].rbA);
        solverConstraints[i].bodyIndexB = mapBatchBody(regB, solverConstraints[i].rbB);
    }

#if PLATFORMATOR_HAS_RUNTIME_AVX2
    if (platformator_solver_simd::cpuSupportsAVX2())
    {
        using namespace platformator_solver_simd;
        struct BatchRanges
        {
            size_t wideBegin = 0;
            size_t wideEnd = 0;
            size_t tailBegin = 0;
            size_t tailEnd = 0;
        };

        solverWideBodyVels.resize(solverBodyStates.size());
        for (size_t i = 0; i < solverBodyStates.size(); ++i)
        {
            const SolverBodyState &s = solverBodyStates[i];
            solverWideBodyVels[i] = WideBodyVel{s.velocity.x(), s.velocity.y(), s.angularVelocity, 0.0f};
        }

        thread_local std::vector<WideConstraint8> wideBlocks;
        thread_local std::vector<size_t> tailIndices;
        thread_local std::vector<BatchRanges> batchRanges;
        thread_local std::vector<size_t> blockBucket;
        thread_local std::vector<size_t> singleBucket;

        wideBlocks.clear();
        tailIndices.clear();
        batchRanges.resize(solverBatchCount);

        const auto packBucket = [&](const std::vector<size_t> &bucket) {
            size_t pos = 0;
            while (pos + 8 <= bucket.size())
            {
                wideBlocks.emplace_back();
                WideConstraint8 &wide = wideBlocks.back();
                wide.blockMask = 0;
                wide.twoPointMask = 0;
                wide.writeMaskA = 0;
                wide.writeMaskB = 0;
                for (size_t lane = 0; lane < 8; ++lane)
                {
                    const SolverConstraint &sc = solverConstraints[bucket[pos + lane]];
                    const SolverBodyState &bA = solverBodyStates[sc.bodyIndexA];
                    const SolverBodyState &bB = solverBodyStates[sc.bodyIndexB];
                    packWideLane(wide, lane, &sc, bA.inverseMass, bA.inverseMomentOfInertia, bB.inverseMass,
                                 bB.inverseMomentOfInertia);
                }
                pos += 8;
            }
            if (bucket.size() - pos >= 4)
            {
                wideBlocks.emplace_back();
                WideConstraint8 &wide = wideBlocks.back();
                wide.blockMask = 0;
                wide.twoPointMask = 0;
                wide.writeMaskA = 0;
                wide.writeMaskB = 0;
                const size_t rem = bucket.size() - pos;
                for (size_t lane = 0; lane < 8; ++lane)
                {
                    if (lane < rem)
                    {
                        const SolverConstraint &sc = solverConstraints[bucket[pos + lane]];
                        const SolverBodyState &bA = solverBodyStates[sc.bodyIndexA];
                        const SolverBodyState &bB = solverBodyStates[sc.bodyIndexB];
                        packWideLane(wide, lane, &sc, bA.inverseMass, bA.inverseMomentOfInertia, bB.inverseMass,
                                     bB.inverseMomentOfInertia);
                    }
                    else
                    {
                        packWideLane<SolverConstraint>(wide, lane, nullptr);
                    }
                }
                pos = bucket.size();
            }
            while (pos < bucket.size())
            {
                tailIndices.push_back(bucket[pos++]);
            }
        };

        for (size_t batch = 0; batch < solverBatchCount; ++batch)
        {
            const auto &order = solverBatches[batch];
            BatchRanges &ranges = batchRanges[batch];
            ranges.wideBegin = wideBlocks.size();
            ranges.tailBegin = tailIndices.size();

            if (order.size() < 4)
            {
                tailIndices.insert(tailIndices.end(), order.begin(), order.end());
            }
            else
            {
                blockBucket.clear();
                singleBucket.clear();
                for (size_t index : order)
                {
                    const SolverConstraint &c = solverConstraints[index];
                    if (c.blockSolve)
                    {
                        blockBucket.push_back(index);
                    }
                    else if (c.pointCount == 1)
                    {
                        singleBucket.push_back(index);
                    }
                    else
                    {
                        tailIndices.push_back(index);
                    }
                }
                packBucket(blockBucket);
                packBucket(singleBucket);
            }

            ranges.wideEnd = wideBlocks.size();
            ranges.tailEnd = tailIndices.size();
        }

        WideConstraint8 *wideData = wideBlocks.data();
        WideBodyVel *bodyData = solverWideBodyVels.data();
        constexpr size_t parallelWideThreshold = parallelSolverBatchSize / 8;

        const auto runTailWarmStart = [&](const SolverConstraint &c) {
            WideBodyVel &vA = bodyData[c.bodyIndexA];
            WideBodyVel &vB = bodyData[c.bodyIndexB];
            SolverBodyState sA{{vA.vx, vA.vy}, vA.w, solverBodyStates[c.bodyIndexA].inverseMass,
                               solverBodyStates[c.bodyIndexA].inverseMomentOfInertia};
            SolverBodyState sB{{vB.vx, vB.vy}, vB.w, solverBodyStates[c.bodyIndexB].inverseMass,
                               solverBodyStates[c.bodyIndexB].inverseMomentOfInertia};
            applyWarmStart(c, sA, sB);
            vA.vx = sA.velocity.x();
            vA.vy = sA.velocity.y();
            vA.w = sA.angularVelocity;
            vB.vx = sB.velocity.x();
            vB.vy = sB.velocity.y();
            vB.w = sB.angularVelocity;
        };

        const auto runTailResolve = [&](SolverConstraint &c) {
            WideBodyVel &vA = bodyData[c.bodyIndexA];
            WideBodyVel &vB = bodyData[c.bodyIndexB];
            SolverBodyState sA{{vA.vx, vA.vy}, vA.w, solverBodyStates[c.bodyIndexA].inverseMass,
                               solverBodyStates[c.bodyIndexA].inverseMomentOfInertia};
            SolverBodyState sB{{vB.vx, vB.vy}, vB.w, solverBodyStates[c.bodyIndexB].inverseMass,
                               solverBodyStates[c.bodyIndexB].inverseMomentOfInertia};
            resolveCollision(c, sA, sB);
            vA.vx = sA.velocity.x();
            vA.vy = sA.velocity.y();
            vA.w = sA.angularVelocity;
            vB.vx = sB.velocity.x();
            vB.vy = sB.velocity.y();
            vB.w = sB.angularVelocity;
        };

        for (size_t batch = 0; batch < solverBatchCount; ++batch)
        {
            const BatchRanges &r = batchRanges[batch];
            const size_t wideCount = r.wideEnd - r.wideBegin;
            if (wideCount < parallelWideThreshold)
            {
                for (size_t w = r.wideBegin; w < r.wideEnd; ++w)
                {
                    applyWarmStartWide8AVX2(wideData[w], bodyData);
                }
            }
            else
            {
                tbb::parallel_for(
                    tbb::blocked_range<size_t>(r.wideBegin, r.wideEnd, 4),
                    [=](const auto &range) {
                        for (size_t w = range.begin(); w != range.end(); ++w)
                        {
                            applyWarmStartWide8AVX2(wideData[w], bodyData);
                        }
                    },
                    context);
            }
            for (size_t t = r.tailBegin; t < r.tailEnd; ++t)
            {
                runTailWarmStart(solverConstraints[tailIndices[t]]);
            }
        }

        for (size_t iteration = 0; iteration < COLLISION_SOLVER_ITERATIONS; ++iteration)
        {
            for (size_t batch = 0; batch < solverBatchCount; ++batch)
            {
                const BatchRanges &r = batchRanges[batch];
                const size_t wideCount = r.wideEnd - r.wideBegin;
                if (wideCount < parallelWideThreshold)
                {
                    for (size_t w = r.wideBegin; w < r.wideEnd; ++w)
                    {
                        resolveWide8AVX2(wideData[w], bodyData);
                    }
                }
                else
                {
                    tbb::parallel_for(
                        tbb::blocked_range<size_t>(r.wideBegin, r.wideEnd, 4),
                        [=](const auto &range) {
                            for (size_t w = range.begin(); w != range.end(); ++w)
                            {
                                resolveWide8AVX2(wideData[w], bodyData);
                            }
                        },
                        context);
                }
                for (size_t t = r.tailBegin; t < r.tailEnd; ++t)
                {
                    runTailResolve(solverConstraints[tailIndices[t]]);
                }
            }
        }

        for (const WideConstraint8 &wide : wideBlocks)
        {
            storeWideImpulses(wide);
        }
        for (size_t index : tailIndices)
        {
            storeConstraintImpulses(solverConstraints[index]);
        }
        for (const auto &[regIdx, rb] : solverTouchedWritableBodies)
        {
            const uint32_t localIdx = solverBodyLocalIndices[regIdx];
            const WideBodyVel &state = solverWideBodyVels[localIdx];
            rb->velocity.x() = state.vx;
            rb->velocity.y() = state.vy;
            rb->angularVelocity = state.w;
            solverBodyLocalIndices[regIdx] = UNMAPPED_SOLVER_BODY;
        }
        return;
    }
#endif

    for (size_t batch = 0; batch < solverBatchCount; ++batch)
    {
        const auto &order = solverBatches[batch];
        if (order.size() < parallelSolverBatchSize)
        {
            for (size_t index : order)
            {
                const SolverConstraint &c = solverConstraints[index];
                applyWarmStart(c, solverBodyStates[c.bodyIndexA], solverBodyStates[c.bodyIndexB]);
            }
        }
        else
        {
            tbb::parallel_for(
                tbb::blocked_range<size_t>(0, order.size(), 32),
                [&](const auto &range) {
                    for (size_t i = range.begin(); i != range.end(); ++i)
                    {
                        const SolverConstraint &c = solverConstraints[order[i]];
                        applyWarmStart(c, solverBodyStates[c.bodyIndexA], solverBodyStates[c.bodyIndexB]);
                    }
                },
                context);
        }
    }
    for (size_t iteration = 0; iteration < COLLISION_SOLVER_ITERATIONS; ++iteration)
    {
    for (size_t batch = 0; batch < solverBatchCount; ++batch)
        {
            const auto &order = solverBatches[batch];
            if (order.size() < parallelSolverBatchSize)
            {
                for (size_t index : order)
                {
                    SolverConstraint &c = solverConstraints[index];
                    resolveCollision(c, solverBodyStates[c.bodyIndexA], solverBodyStates[c.bodyIndexB]);
                }
            }
            else
            {
                tbb::parallel_for(
                    tbb::blocked_range<size_t>(0, order.size(), 32),
                    [&](const auto &range) {
                        for (size_t i = range.begin(); i != range.end(); ++i)
                        {
                            SolverConstraint &c = solverConstraints[order[i]];
                            resolveCollision(c, solverBodyStates[c.bodyIndexA], solverBodyStates[c.bodyIndexB]);
                        }
                    },
                    context);
            }
        }
    }

    for (const SolverConstraint &constraint : solverConstraints)
    {
        storeConstraintImpulses(constraint);
    }
    for (const auto &[regIdx, rb] : solverTouchedWritableBodies)
    {
        const uint32_t localIdx = solverBodyLocalIndices[regIdx];
        const SolverBodyState &state = solverBodyStates[localIdx];
        rb->velocity = state.velocity;
        rb->angularVelocity = state.angularVelocity;
        solverBodyLocalIndices[regIdx] = UNMAPPED_SOLVER_BODY;
    }
}

void PhysicsManager::prepareSolverConstraint(const SolverPreStepJob &job, float inverseTimeDelta,
                                             SolverConstraint &outConstraint) const
{
    Collision *collision = job.collision;
    Rigidbody *rbA = job.rbA;
    Rigidbody *rbB = job.rbB;

    const float invMassA = rbA->getIsSleeping() ? 0.f : rbA->getInverseMass();
    const float invMassB = rbB->getIsSleeping() ? 0.f : rbB->getInverseMass();
    const float invInertiaA = rbA->getIsSleeping() ? 0.f : rbA->getInverseMomentOfInertia();
    const float invInertiaB = rbB->getIsSleeping() ? 0.f : rbB->getInverseMomentOfInertia();

    const Eigen::Vector2f &positionA = rbA->getGameObject()->worldPosition;
    const Eigen::Vector2f &positionB = rbB->getGameObject()->worldPosition;

    const Eigen::Vector2f &normal = collision->getNormal();
    const Eigen::Vector2f tangent = crossSV(1.0f, normal);
    const float inverseMassSum = invMassA + invMassB;
    ClipPointsWithData &contactPoints = collision->getContactPoints();
    const uint8_t pointCount = static_cast<uint8_t>(std::min<size_t>(contactPoints.count, 2));

    const float frictionA = rbA->getFriction();
    const float frictionB = rbB->getFriction();
    const float combinedFriction = (frictionA == frictionB) ? frictionA : std::sqrt(frictionA * frictionB);
    const float combinedRestitution = std::max(rbA->getRestitution(), rbB->getRestitution());

    outConstraint.rbA = rbA;
    outConstraint.rbB = rbB;
    outConstraint.contactPoints = &contactPoints;
    outConstraint.normal = normal;
    outConstraint.tangent = tangent;
    outConstraint.solverFriction = combinedFriction;
    outConstraint.pointCount = pointCount;
    outConstraint.blockSolve = false;

    for (uint8_t i = 0; i < pointCount; ++i)
    {
        outConstraint.rA[i] = contactPoints.points[i].point.point - positionA;
        outConstraint.rB[i] = contactPoints.points[i].point.point - positionB;
    }

    if (pointCount == 2)
    {
        const float a0 = cross2D(outConstraint.rA[0], normal), a1 = cross2D(outConstraint.rA[1], normal);
        const float b0 = cross2D(outConstraint.rB[0], normal), b1 = cross2D(outConstraint.rB[1], normal);
        const float k00 = inverseMassSum + invInertiaA * a0 * a0 + invInertiaB * b0 * b0;
        const float k11 = inverseMassSum + invInertiaA * a1 * a1 + invInertiaB * b1 * b1;
        const float k01 = inverseMassSum + invInertiaA * a0 * a1 + invInertiaB * b0 * b1;
        const float determinant = k00 * k11 - k01 * k01;
        if (determinant > 0 && k00 * k00 < 1000.f * determinant)
        {
            outConstraint.normalK << k00, k01, k01, k11;
            outConstraint.normalMass << k11, -k01, -k01, k00;
            outConstraint.normalMass /= determinant;
            outConstraint.blockSolve = true;
        }
    }

    const Eigen::Vector2f &velA = rbA->getVelocity();
    const Eigen::Vector2f &velB = rbB->getVelocity();
    const float angVelA = rbA->getAngularVelocity();
    const float angVelB = rbB->getAngularVelocity();

    for (uint8_t i = 0; i < pointCount; ++i)
    {
        const Eigen::Vector2f &rA = outConstraint.rA[i];
        const Eigen::Vector2f &rB = outConstraint.rB[i];
        const float rA2 = rA.dot(rA);
        const float rB2 = rB.dot(rB);
        const float rnA = rA.dot(normal);
        const float rnB = rB.dot(normal);
        const float kNormal = inverseMassSum + invInertiaA * (rA2 - rnA * rnA) + invInertiaB * (rB2 - rnB * rnB);
        outConstraint.massNormal[i] = kNormal > 0 ? 1.0f / kNormal : 0.f;

        const float rtA = rA.dot(tangent);
        const float rtB = rB.dot(tangent);
        const float kTangent = inverseMassSum + invInertiaA * (rA2 - rtA * rtA) + invInertiaB * (rB2 - rtB * rtB);
        outConstraint.massTangent[i] = kTangent > 0 ? 1.0f / kTangent : 0.f;

        const float baumgarteBias =
            -COLLISION_BIAS_FACTOR * inverseTimeDelta *
            std::min(0.0f, contactPoints.points[i].separation + COLLISION_ALLOWED_PENETRATION);

        const Eigen::Vector2f relativeVelocity = (velB + crossSV(angVelB, rB)) - (velA + crossSV(angVelA, rA));
        const float vn = relativeVelocity.dot(normal);
        float restitutionBias = 0.0f;
        if (vn < -RESTITUTION_VELOCITY_THRESHOLD)
        {
            restitutionBias = -combinedRestitution * vn;
        }

        outConstraint.bias[i] = baumgarteBias + restitutionBias;
        outConstraint.accumulatedNormalImpulse[i] = contactPoints.points[i].accumulatedNormalImpulse;
        outConstraint.accumulatedTangentImpulse[i] = contactPoints.points[i].accumulatedTangentImpulse;
    }
}

void PhysicsManager::applyWarmStart(const SolverConstraint &constraint, SolverBodyState &bodyA,
                                    SolverBodyState &bodyB) noexcept
{
    const float invMassA = bodyA.inverseMass;
    const float invInertiaA = bodyA.inverseMomentOfInertia;
    const float invMassB = bodyB.inverseMass;
    const float invInertiaB = bodyB.inverseMomentOfInertia;

    Eigen::Vector2f vA = bodyA.velocity;
    float wA = bodyA.angularVelocity;
    Eigen::Vector2f vB = bodyB.velocity;
    float wB = bodyB.angularVelocity;

    const Eigen::Vector2f &normal = constraint.normal;
    const Eigen::Vector2f &tangent = constraint.tangent;
    for (uint8_t i = 0; i < constraint.pointCount; ++i)
    {
        const Eigen::Vector2f accumulatedImpulse =
            constraint.accumulatedNormalImpulse[i] * normal + constraint.accumulatedTangentImpulse[i] * tangent;
        applyLocalImpulse(vA, wA, invMassA, invInertiaA, -accumulatedImpulse, constraint.rA[i]);
        applyLocalImpulse(vB, wB, invMassB, invInertiaB, accumulatedImpulse, constraint.rB[i]);
    }

    if (invMassA != 0.0f || invInertiaA != 0.0f)
    {
        bodyA.velocity = vA;
        bodyA.angularVelocity = wA;
    }
    if (invMassB != 0.0f || invInertiaB != 0.0f)
    {
        bodyB.velocity = vB;
        bodyB.angularVelocity = wB;
    }
}

void PhysicsManager::resolveCollision(SolverConstraint &constraint, SolverBodyState &bodyA,
                                      SolverBodyState &bodyB) noexcept
{
    const float invMassA = bodyA.inverseMass;
    const float invInertiaA = bodyA.inverseMomentOfInertia;
    const float invMassB = bodyB.inverseMass;
    const float invInertiaB = bodyB.inverseMomentOfInertia;

    Eigen::Vector2f vA = bodyA.velocity;
    float wA = bodyA.angularVelocity;
    Eigen::Vector2f vB = bodyB.velocity;
    float wB = bodyB.angularVelocity;

    const Eigen::Vector2f &normal = constraint.normal;
    const Eigen::Vector2f &tangent = constraint.tangent;

    if (constraint.blockSolve)
    {
        const Eigen::Vector2f previous(constraint.accumulatedNormalImpulse[0], constraint.accumulatedNormalImpulse[1]);
        Eigen::Vector2f rhs;
        for (int i = 0; i < 2; ++i)
        {
            rhs[i] = ((vB + crossSV(wB, constraint.rB[i])) - (vA + crossSV(wA, constraint.rA[i]))).dot(normal) -
                     constraint.bias[i];
        }
        rhs -= constraint.normalK * previous;
        Eigen::Vector2f next = -constraint.normalMass * rhs;
        bool accepted = next.x() >= 0 && next.y() >= 0;
        if (!accepted)
        {
            next = {-rhs.x() / constraint.normalK(0, 0), 0};
            accepted = next.x() >= 0 && constraint.normalK(1, 0) * next.x() + rhs.y() >= 0;
        }
        if (!accepted)
        {
            next = {0, -rhs.y() / constraint.normalK(1, 1)};
            accepted = next.y() >= 0 && constraint.normalK(0, 1) * next.y() + rhs.x() >= 0;
        }
        if (!accepted && rhs.x() >= 0 && rhs.y() >= 0)
        {
            next.setZero();
            accepted = true;
        }
        if (accepted)
        {
            for (int i = 0; i < 2; ++i)
            {
                const Eigen::Vector2f impulse = (next[i] - previous[i]) * normal;
                applyLocalImpulse(vA, wA, invMassA, invInertiaA, -impulse, constraint.rA[i]);
                applyLocalImpulse(vB, wB, invMassB, invInertiaB, impulse, constraint.rB[i]);
                constraint.accumulatedNormalImpulse[i] = next[i];
            }
        }

        for (uint8_t i = 0; i < 2; ++i)
        {
            const Eigen::Vector2f &rA = constraint.rA[i];
            const Eigen::Vector2f &rB = constraint.rB[i];
            const Eigen::Vector2f relativeVelocity = (vB + crossSV(wB, rB)) - (vA + crossSV(wA, rA));
            const float vt = relativeVelocity.dot(tangent);
            float tangentImpulse = constraint.massTangent[i] * (-vt);

            const float oldTangentImpulse = constraint.accumulatedTangentImpulse[i];
            const float maxTangentImpulse = constraint.solverFriction * constraint.accumulatedNormalImpulse[i];
            const float newTangentImpulse =
                Clamp(oldTangentImpulse + tangentImpulse, -maxTangentImpulse, maxTangentImpulse);
            constraint.accumulatedTangentImpulse[i] = newTangentImpulse;
            tangentImpulse = newTangentImpulse - oldTangentImpulse;

            const Eigen::Vector2f contactImpulse = tangentImpulse * tangent;
            applyLocalImpulse(vA, wA, invMassA, invInertiaA, -contactImpulse, rA);
            applyLocalImpulse(vB, wB, invMassB, invInertiaB, contactImpulse, rB);
        }
    }
    else
    {
        for (uint8_t i = 0; i < constraint.pointCount; ++i)
        {
            const Eigen::Vector2f &rA = constraint.rA[i];
            const Eigen::Vector2f &rB = constraint.rB[i];

            Eigen::Vector2f relativeVelocity = (vB + crossSV(wB, rB)) - (vA + crossSV(wA, rA));
            const float vn = relativeVelocity.dot(normal);
            float normalImpulse = constraint.massNormal[i] * (-vn + constraint.bias[i]);

            const float accumulatedNormalImpulse = constraint.accumulatedNormalImpulse[i];
            const float maxNormalImpulse = std::max(accumulatedNormalImpulse + normalImpulse, 0.0f);
            constraint.accumulatedNormalImpulse[i] = maxNormalImpulse;
            normalImpulse = maxNormalImpulse - accumulatedNormalImpulse;

            const Eigen::Vector2f impulse = normalImpulse * normal;
            applyLocalImpulse(vA, wA, invMassA, invInertiaA, -impulse, rA);
            applyLocalImpulse(vB, wB, invMassB, invInertiaB, impulse, rB);

            relativeVelocity = (vB + crossSV(wB, rB)) - (vA + crossSV(wA, rA));
            const float vt = relativeVelocity.dot(tangent);
            float tangentImpulse = constraint.massTangent[i] * (-vt);

            const float oldTangentImpulse = constraint.accumulatedTangentImpulse[i];
            const float maxTangentImpulse = constraint.solverFriction * maxNormalImpulse;
            const float newTangentImpulse =
                Clamp(oldTangentImpulse + tangentImpulse, -maxTangentImpulse, maxTangentImpulse);
            constraint.accumulatedTangentImpulse[i] = newTangentImpulse;
            tangentImpulse = newTangentImpulse - oldTangentImpulse;

            const Eigen::Vector2f contactImpulse = tangentImpulse * tangent;
            applyLocalImpulse(vA, wA, invMassA, invInertiaA, -contactImpulse, rA);
            applyLocalImpulse(vB, wB, invMassB, invInertiaB, contactImpulse, rB);
        }
    }

    if (invMassA != 0.0f || invInertiaA != 0.0f)
    {
        bodyA.velocity = vA;
        bodyA.angularVelocity = wA;
    }
    if (invMassB != 0.0f || invInertiaB != 0.0f)
    {
        bodyB.velocity = vB;
        bodyB.angularVelocity = wB;
    }
}

void PhysicsManager::storeConstraintImpulses(const SolverConstraint &constraint) noexcept
{
    ClipPointsWithData &contactPoints = *constraint.contactPoints;
    for (uint8_t i = 0; i < constraint.pointCount; ++i)
    {
        contactPoints.points[i].accumulatedNormalImpulse = constraint.accumulatedNormalImpulse[i];
        contactPoints.points[i].accumulatedTangentImpulse = constraint.accumulatedTangentImpulse[i];
    }
}

void PhysicsManager::resolveKinematicCollision(const Collision *collision, Rigidbody *rbA, Rigidbody *rbB)
{
    const bool referenceIsKinematic = rbA->getBodyType() == BodyType::KINEMATIC;
    const bool incidentIsKinematic = rbB->getBodyType() == BodyType::KINEMATIC;

    float maxPenetrationDepth = 0.0f;
    const ClipPointsWithData &contactPoints = collision->getContactPoints();
    for (size_t i = 0; i < contactPoints.count; ++i)
    {
        maxPenetrationDepth = std::max(maxPenetrationDepth, -contactPoints.points[i].separation);
    }

    float correctionDistance = std::max(0.0f, maxPenetrationDepth - COLLISION_ALLOWED_PENETRATION);
    float perBodyCorrection = correctionDistance;
    if (referenceIsKinematic && incidentIsKinematic)
    {
        perBodyCorrection *= 0.5f;
    }

    const Eigen::Vector2f &normal = collision->getNormal();
    if (referenceIsKinematic)
    {
        resolveKinematicBodyAgainstNormal(rbA, normal, perBodyCorrection);
    }
    if (incidentIsKinematic)
    {
        resolveKinematicBodyAgainstNormal(rbB, -normal, perBodyCorrection);
    }
}

void PhysicsManager::resolveKinematicBodyAgainstNormal(Rigidbody *rigidBody, const Eigen::Vector2f &approachNormal,
                                                       float correctionDistance)
{
    Eigen::Vector2f velocity = rigidBody->getVelocity();
    float normalSpeed = velocity.dot(approachNormal);
    if (normalSpeed > 0.0f)
    {
        rigidBody->setVelocity(velocity - normalSpeed * approachNormal);
    }

    if (correctionDistance > 0.0f)
    {
        GameObject *gameObject = rigidBody->getGameObject();
        gameObject->position -= correctionDistance * approachNormal;
        platformator_detail::RuntimeAccess::gameManager().queueTransform(gameObject, false);
    }
}
