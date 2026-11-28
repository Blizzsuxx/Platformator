#include "physics/physicsmanager.h"

#include "benchmark/benchmark.h"
#include "benchmark/workprobe.h"
#include "core/constants.h"
#include "physics/physicsgeometry.h"
#include "physics/physicstaskcontext.h"
#include "rendering/debugdraw.h"

#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/task_group.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>

// ============================================================================
// Broad-Phase & Narrow-Phase Pipeline
// ============================================================================

void PhysicsManager::checkForCollisions()
{
    broadPhase();
    narrowPhase();
}

void PhysicsManager::broadPhase()
{
    PLATFORMATOR_BENCH_SCOPE(BroadPhase);

    grid.flushPendingCellUpdates();

    PLATFORMATOR_BENCH_SET_COUNTER(OccupiedCellCount, grid.getCells().size());
    PLATFORMATOR_BENCH_SET_COUNTER(CandidatePairCount, grid.getCandidatePairCount());
}

bool PhysicsManager::needsNarrowPhaseTransition(const ColliderPair *pair, bool noPendingBodyChanges) const noexcept
{
    const auto *collision = pair->getCollision();
    const bool hasContact = collision != nullptr;
    if (pair->resultHadContact != hasContact)
    {
        return true;
    }
    if (!hasContact && !pair->isInSolver() && !pair->contributedSupportBodyA && !pair->contributedSupportBodyB &&
        pair->contactIndex == ColliderPair::INVALID_INDEX)
    {
        return false;
    }
    if (hasContact && pair->contactIndex != ColliderPair::INVALID_INDEX)
    {
        if (!pair->isInSolver() && !pair->contributedSupportBodyA && !pair->contributedSupportBodyB)
        {
            const Collider *a = pair->getObjectA();
            const Collider *b = pair->getObjectB();
            if (a->getIsTrigger() || b->getIsTrigger() ||
                (noPendingBodyChanges && !a->hasNonStaticRigidbody() && !b->hasNonStaticRigidbody()))
            {
                return false;
            }
        }
        if (noPendingBodyChanges && pair->isInSolver())
        {
            const auto [supportsA, supportsB] = pairSupportFlags(pair, collision);
            if ((pair->contributedSupportBodyA != nullptr) == supportsA &&
                (pair->contributedSupportBodyB != nullptr) == supportsB)
            {
                return false;
            }
        }
    }
    return true;
}

void PhysicsManager::applyNarrowPhaseTransition(ColliderPair *pair)
{
    auto *collision = pair->getCollision();
    const bool hasContact = collision != nullptr;
    if (pair->resultHadContact != hasContact)
    {
        const auto type = hasContact ? PhysicsEvent::COLLISION_ENTER : PhysicsEvent::COLLISION_EXIT;
        addPendingPhysicsEvent(makePhysicsEvent(type, pair, collision));
    }
    reconcilePairSupport(pair);
    refreshContactParticipation(pair);
}

void PhysicsManager::emitNarrowPhaseDebugDraw() const
{
#if PLATFORMATOR_ENABLE_DEBUG_TOOLS
    for (int32_t islandId : activeIslandIds)
    {
        for (const ColliderPair *pair : persistentIslands[static_cast<size_t>(islandId)].contacts)
        {
            if (auto *collision = pair->getCollision())
            {
                DebugDraw::getInstance().addCollisionDebugObject(*collision);
            }
        }
    }
    for (const ColliderPair *pair : kinematicSolverPairs)
    {
        if (auto *collision = pair->getCollision())
        {
            DebugDraw::getInstance().addCollisionDebugObject(*collision);
        }
    }
#endif
}

void PhysicsManager::narrowPhase()
{
    PLATFORMATOR_BENCH_SCOPE(NarrowPhase);

    const auto &pendingPairs = *grid.getPendingNarrowPhasePairs();
    const size_t pairCount = pendingPairs.size();
    const bool noPendingBodyChanges = pendingBodyChanges.empty();

    if (pairCount < NARROW_PHASE_PARALLEL_THRESHOLD || !hasParallelPhysicsWorkers())
    {
        for (size_t i = 0; i < pairCount; ++i)
        {
            if (i + 4 < pairCount)
            {
                platformator_detail::prefetchColliderPair(pendingPairs[i + 4]);
            }
            ColliderPair *pair = pendingPairs[i];
            processNarrowPhasePair(pair);
            PLATFORMATOR_WORK_ADD(geometryPairs, pair->resultTestedGeometry ? 1 : 0);
            PLATFORMATOR_WORK_ADD(contactTransitionChecks, 1);
            if (needsNarrowPhaseTransition(pair, noPendingBodyChanges))
            {
                applyNarrowPhaseTransition(pair);
            }
        }
    }
    else
    {
        const size_t chunkCount = std::min<size_t>(32, (pairCount + 63) / 64);
        if (narrowScratch.transitionChunks.size() < chunkCount)
        {
            narrowScratch.transitionChunks.resize(chunkCount);
        }
#if PLATFORMATOR_ENABLE_WORK_PROBES
        narrowScratch.geometryCounts.assign(chunkCount, 0);
#endif
        const size_t base = pairCount / chunkCount;
        const size_t rem = pairCount % chunkCount;

        tbb::task_group_context context(tbb::task_group_context::isolated);
        tbb::parallel_for(
            size_t(0), chunkCount,
            [&](size_t chunkIdx) {
                auto &transitions = narrowScratch.transitionChunks[chunkIdx];
                transitions.clear();
                const size_t start = chunkIdx * base + std::min(chunkIdx, rem);
                const size_t end = start + base + (chunkIdx < rem ? 1 : 0);
#if PLATFORMATOR_ENABLE_WORK_PROBES
                uint64_t tested = 0;
#endif
                for (size_t i = start; i < end; ++i)
                {
                    if (i + 4 < end)
                    {
                        platformator_detail::prefetchColliderPair(pendingPairs[i + 4]);
                    }
                    ColliderPair *pair = pendingPairs[i];
                    processNarrowPhasePair(pair);
#if PLATFORMATOR_ENABLE_WORK_PROBES
                    if (pair->resultTestedGeometry)
                    {
                        ++tested;
                    }
#endif
                    if (needsNarrowPhaseTransition(pair, noPendingBodyChanges))
                    {
                        transitions.push_back(pair);
                    }
                }
#if PLATFORMATOR_ENABLE_WORK_PROBES
                narrowScratch.geometryCounts[chunkIdx] = tested;
#endif
            },
            context);

#if PLATFORMATOR_ENABLE_WORK_PROBES
        uint64_t totalTested = 0;
        for (size_t c = 0; c < chunkCount; ++c)
        {
            totalTested += narrowScratch.geometryCounts[c];
        }
        PLATFORMATOR_WORK_ADD(geometryPairs, totalTested);
        PLATFORMATOR_WORK_ADD(contactTransitionChecks, pairCount);
#endif

        for (size_t c = 0; c < chunkCount; ++c)
        {
            for (ColliderPair *pair : narrowScratch.transitionChunks[c])
            {
                applyNarrowPhaseTransition(pair);
            }
        }
    }

    flushSupportDeltas();
    emitNarrowPhaseDebugDraw();

#if PLATFORMATOR_ENABLE_BENCHMARKS
    PLATFORMATOR_BENCH_SET_COUNTER(PendingNarrowPhasePairCount, pairCount);
    PLATFORMATOR_BENCH_SET_COUNTER(ActiveCollisionCount, contactPairs.size());
#endif

    grid.clearPendingNarrowPhasePairs();
}

void PhysicsManager::processNarrowPhasePair(ColliderPair *pair)
{
    // Structural queue mutation/erasure cannot overlap this distinct-pair worker batch.
    pair->setNarrowPhaseQueueIndex(SIZE_MAX);
    pair->resultHadContact = pair->getCollision() != nullptr;
#if PLATFORMATOR_ENABLE_WORK_PROBES
    pair->resultTestedGeometry = false;
#endif
    const Collider *colA = pair->getObjectA();
    const Collider *colB = pair->getObjectB();
    if (colA->isRetired() || colB->isRetired() || !colA->getIsOwnerActive() || !colB->getIsOwnerActive())
    {
        pair->clearCollisionNoEvent();
        return;
    }
    if (pair->shouldUpdate())
    {
#if PLATFORMATOR_ENABLE_WORK_PROBES
        pair->resultTestedGeometry = true;
#endif
        physics_geometry::updateContactGeometry(*pair, gravityVectorNormalized);
    }
}
