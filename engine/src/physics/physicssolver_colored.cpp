#include "physics/physicsmanager.h"
#include "benchmark/workprobe.h"

#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/partitioner.h>
#include <oneapi/tbb/task_group.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>

// ============================================================================
// Graph-Colored Large-Island Soft Step Solver (Deterministic w=1 == w=N)
// ============================================================================

bool PhysicsManager::solveColoredIsland(PersistentIsland &island, const SoftStepParams &params, bool allowParallel)
{
    ensureIslandColorGraph(island);
    const IslandColorGraph &graph = *island.colorGraph;

    solverBodyStates.clear();
    solverBodyForces.clear();
    solverBodyPrep.clear();
    solverKinematicBodies.clear();
    solverTouchedWritableBodies.clear();
    if (solverBodyStates.capacity() < island.bodies.size() + 1)
    {
        solverBodyStates.reserve(island.bodies.size() + 1);
        solverBodyForces.reserve(island.bodies.size() + 1);
        solverBodyPrep.reserve(island.bodies.size() + 1);
    }
    solverBodyStates.push_back(SolverBodyState{}); // slot 0: static/sleeping read-only
    solverBodyForces.push_back(SolverBodyForce{});
    solverBodyPrep.push_back(BodyPrepCache{});

    ScopedSolverBodyMapGuard mapGuard{solverBodyLocalIndices, solverTouchedWritableBodies};

    for (Rigidbody *rb : island.bodies)
    {
        if (!rb->isAwakeDynamic())
        {
            continue;
        }
        const size_t regIdx = rb->getPhysicsManagerIndex();
        if (regIdx >= registeredBodies.size() || registeredBodies[regIdx] != rb)
        {
            return false;
        }
        uint32_t &mapped = solverBodyLocalIndices[regIdx];
        if (mapped == UNMAPPED_SOLVER_BODY)
        {
            mapped = static_cast<uint32_t>(solverBodyStates.size());
            solverBodyStates.push_back(makeDynamicBodyState(rb));
            solverBodyForces.push_back(makeBodyForce(rb));
            solverBodyPrep.push_back(makeBodyPrepCache(rb));
            solverTouchedWritableBodies.push_back(WritableSolverBody{regIdx, mapped, rb});
        }
    }

    const bool hasMovingKinematic = !movingKinematicBodies.empty();
    tbb::task_group_context context(tbb::task_group_context::isolated);

#if PLATFORMATOR_HAS_RUNTIME_AVX2
    const auto movingKinematic = [](const Collider *collider) {
        const auto *body = collider->getAttachedRigidbody();
        return body && body->isMovingKinematic();
    };
    if (platformator_solver_simd::cpuSupportsAVX2() &&
        (!hasMovingKinematic || std::ranges::none_of(island.contacts, [&](const ColliderPair *pair) {
             PLATFORMATOR_WORK_ADD(solverColoredEligibilityContactVisits, 1);
             return movingKinematic(pair->getObjectA()) || movingKinematic(pair->getObjectB());
         })))
    {
        using namespace platformator_solver_simd;
        auto *wideBuckets = colorWideBlocks.data();
        auto *coldBuckets = colorWideColdBlocks.data();
        auto *tailBuckets = colorTailConstraints.data();

        std::array<uint8_t, IslandColorGraph::MAX_PARALLEL_COLORS> activeColors{};
        size_t activeColorCount = 0;
        for (uint8_t c = 0; c < IslandColorGraph::MAX_PARALLEL_COLORS; ++c)
        {
            if (!graph.buckets[c].empty())
            {
                activeColors[activeColorCount++] = c;
            }
        }

        const auto resolveBodySlotReadOnly = [&](Rigidbody *rb) noexcept -> uint32_t {
            if (rb->isAwakeDynamic())
            {
                const size_t regIdx = rb->getPhysicsManagerIndex();
                if (regIdx < registeredBodies.size() && registeredBodies[regIdx] == rb)
                {
                    return solverBodyLocalIndices[regIdx];
                }
                return UNMAPPED_SOLVER_BODY;
            }
            return 0;
        };

        std::atomic<bool> allMapped{true};
        std::atomic<bool> anyRestitution{false};
        const auto packColorDirect = [&, wideBuckets, coldBuckets, tailBuckets](uint8_t c) {
            const auto &pairs = graph.buckets[c];
            auto &wideOut = wideBuckets[c];
            auto &coldOut = coldBuckets[c];
            auto &tailOut = tailBuckets[c];
            wideOut.clear();
            coldOut.clear();
            tailOut.clear();
            bool localRestitution = false;

            const auto emitBatch = [&](const SolverPreStepJob *batch, size_t count) {
                if (count == 0)
                {
                    return;
                }
                if (count < 4)
                {
                    for (size_t i = 0; i < count; ++i)
                    {
                        tailOut.emplace_back();
                        SolverConstraint &sc = tailOut.back();
                        prepareSolverConstraint(batch[i], params, sc);
                        localRestitution |= constraintHasActiveRestitution(sc, params.restitutionThreshold);
                    }
                    return;
                }
                wideOut.emplace_back();
                coldOut.emplace_back();
                WideConstraint8 &wide = wideOut.back();
                WideConstraint8Cold &cold = coldOut.back();
                wide.resetMasks();
                for (size_t lane = 0; lane < 8; ++lane)
                {
                    if (lane < count)
                    {
                        const SolverPreStepJob &job = batch[lane];
                        localRestitution |= packWideContactLane(
                            wide, cold, lane, job.collision, job.rbA, job.rbB, job.bodyIndexA, job.bodyIndexB,
                            solverBodyStates.data(), solverBodyForces.data(), solverBodyPrep.data(), params);
                    }
                    else
                    {
                        clearWideLane(wide, cold, lane);
                    }
                }
                finalizeWideEffectiveMasses8AVX2(wide, solverBodyStates.data());
            };

            if (pairs.size() >= 4 && wideOut.capacity() < (pairs.size() + 7) / 8 + 1)
            {
                wideOut.reserve((pairs.size() + 7) / 8 + 1);
                coldOut.reserve((pairs.size() + 7) / 8 + 1);
            }
            SolverPreStepJob twoPointBatch[8];
            SolverPreStepJob onePointBatch[8];
            size_t twoPointCount = 0;
            size_t onePointCount = 0;

            const size_t pairCount = pairs.size();
            for (size_t i = 0; i < pairCount; ++i)
            {
                if (i + 4 < pairCount)
                {
                    __builtin_prefetch(pairs[i + 4], 0, 1);
                }
                Collision *collision = nullptr;
                Rigidbody *rbA = nullptr;
                Rigidbody *rbB = nullptr;
                if (!extractSolverPair(pairs[i], collision, rbA, rbB))
                {
                    continue;
                }
                const size_t pointCount = collision->getContactPoints().count;
                if (pointCount == 0)
                {
                    continue;
                }
                const uint32_t idxA = resolveBodySlotReadOnly(rbA);
                const uint32_t idxB = resolveBodySlotReadOnly(rbB);
                if (idxA == UNMAPPED_SOLVER_BODY || idxB == UNMAPPED_SOLVER_BODY)
                {
                    allMapped.store(false, std::memory_order_relaxed);
                    return;
                }
                const SolverPreStepJob job{collision, rbA, rbB, idxA, idxB};
                if (pointCount >= 2)
                {
                    twoPointBatch[twoPointCount++] = job;
                    if (twoPointCount == 8)
                    {
                        emitBatch(twoPointBatch, 8);
                        twoPointCount = 0;
                    }
                }
                else
                {
                    onePointBatch[onePointCount++] = job;
                    if (onePointCount == 8)
                    {
                        emitBatch(onePointBatch, 8);
                        onePointCount = 0;
                    }
                }
            }
            emitBatch(twoPointBatch, twoPointCount);
            emitBatch(onePointBatch, onePointCount);
            if (localRestitution)
            {
                anyRestitution.store(true, std::memory_order_relaxed);
            }
        };

        if (allowParallel && activeColorCount >= 2)
        {
            tbb::parallel_for(
                size_t(0), activeColorCount, [&](size_t ac) { packColorDirect(activeColors[ac]); }, context);
        }
        else
        {
            for (size_t ac = 0; ac < activeColorCount; ++ac)
            {
                packColorDirect(activeColors[ac]);
            }
        }

        overflowConstraints.clear();
        const auto &overflowPairs = graph.buckets[IslandColorGraph::OVERFLOW_COLOR];
        if (!overflowPairs.empty() && allMapped.load(std::memory_order_relaxed))
        {
            overflowConstraints.reserve(overflowPairs.size());
            for (ColliderPair *pair : overflowPairs)
            {
                Collision *collision = nullptr;
                Rigidbody *rbA = nullptr;
                Rigidbody *rbB = nullptr;
                if (!extractSolverPair(pair, collision, rbA, rbB))
                {
                    continue;
                }
                const uint32_t idxA = resolveBodySlotReadOnly(rbA);
                const uint32_t idxB = resolveBodySlotReadOnly(rbB);
                if (idxA == UNMAPPED_SOLVER_BODY || idxB == UNMAPPED_SOLVER_BODY)
                {
                    allMapped.store(false, std::memory_order_relaxed);
                    break;
                }
                overflowConstraints.emplace_back();
                SolverConstraint &sc = overflowConstraints.back();
                prepareSolverConstraint(SolverPreStepJob{collision, rbA, rbB, idxA, idxB}, params, sc);
                if (constraintHasActiveRestitution(sc, params.restitutionThreshold))
                {
                    anyRestitution.store(true, std::memory_order_relaxed);
                }
            }
        }

        if (!allMapped.load(std::memory_order_relaxed))
        {
            return false;
        }

        SolverBodyState *bodyData = solverBodyStates.data();
        const SolverBodyForce *forceData = solverBodyForces.data();
        const size_t bodyCount = solverBodyStates.size();
        const float h = params.h;
        const bool hasRestitution = anyRestitution.load(std::memory_order_relaxed);
        constexpr size_t parallelWideThreshold = 32;

        const auto runBodyRange = [&](auto &&fn) {
            if (!allowParallel || bodyCount < PARALLEL_BODY_SUBSTEP_THRESHOLD)
            {
                fn(size_t{1}, bodyCount);
            }
            else
            {
                tbb::parallel_for(
                    tbb::blocked_range<size_t>(1, bodyCount, 256),
                    [&](const auto &range) { fn(range.begin(), range.end()); }, tbb::static_partitioner(), context);
            }
        };

        const auto runWideColorPass = [&](auto &&wideFn, auto &&scalarFn) {
            for (size_t ac = 0; ac < activeColorCount; ++ac)
            {
                const uint8_t c = activeColors[ac];
                WideConstraint8 *wideData = wideBuckets[c].data();
                const WideConstraint8Cold *coldData = coldBuckets[c].data();
                const size_t wideCount = wideBuckets[c].size();
                if (!allowParallel || wideCount < parallelWideThreshold)
                {
                    for (size_t w = 0; w < wideCount; ++w)
                    {
                        wideFn(wideData[w], coldData[w]);
                    }
                }
                else
                {
                    const size_t grain = std::max<size_t>(8, wideCount / 16);
                    tbb::parallel_for(
                        tbb::blocked_range<size_t>(0, wideCount, grain),
                        [&](const auto &range) {
                            for (size_t w = range.begin(); w != range.end(); ++w)
                            {
                                wideFn(wideData[w], coldData[w]);
                            }
                        },
                        tbb::static_partitioner(), context);
                }
                for (SolverConstraint &sc : tailBuckets[c])
                {
                    scalarFn(sc);
                }
            }
            for (SolverConstraint &sc : overflowConstraints)
            {
                scalarFn(sc);
            }
        };

        runSoftSteps(
            params, hasRestitution,
            [&] {
                runBodyRange([=](size_t begin, size_t end) {
                    integrateSolverVelocities(bodyData, forceData, begin, end, h);
                });
            },
            [&] {
                runWideColorPass(
                    [=](WideConstraint8 &wide, const WideConstraint8Cold &) { applyWarmStartWide8AVX2(wide, bodyData); },
                    [=](SolverConstraint &sc) { applyWarmStart(sc, bodyData[sc.bodyIndexA], bodyData[sc.bodyIndexB]); });
            },
            [&](auto useBias) {
                runWideColorPass(
                    [&](WideConstraint8 &wide, const WideConstraint8Cold &) {
                        solveWide8SoftAVX2<decltype(useBias)::value>(wide, bodyData, params);
                    },
                    [&](SolverConstraint &sc) {
                        solveConstraintSoft<decltype(useBias)::value>(sc, bodyData[sc.bodyIndexA],
                                                                      bodyData[sc.bodyIndexB], params);
                    });
            },
            [&] {
                runBodyRange([=](size_t begin, size_t end) { integrateSolverPositions(bodyData, begin, end, h); });
            },
            [&] {
                runWideColorPass(
                    [&](WideConstraint8 &wide, const WideConstraint8Cold &cold) {
                        applyRestitutionWide8AVX2(wide, cold, bodyData, params);
                    },
                    [&](SolverConstraint &sc) {
                        applyRestitutionSoft(sc, bodyData[sc.bodyIndexA], bodyData[sc.bodyIndexB], params);
                    });
            });

        const auto storeColor = [wideBuckets, coldBuckets, tailBuckets](uint8_t c) {
            const auto &wides = wideBuckets[c];
            const auto &colds = coldBuckets[c];
            for (size_t w = 0; w < wides.size(); ++w)
            {
                storeWideImpulses(wides[w], colds[w]);
            }
            for (const SolverConstraint &sc : tailBuckets[c])
            {
                storeConstraintImpulses(sc);
            }
        };
        if (allowParallel && activeColorCount >= 2)
        {
            tbb::parallel_for(
                size_t(0), activeColorCount, [&](size_t ac) { storeColor(activeColors[ac]); }, context);
        }
        else
        {
            for (size_t ac = 0; ac < activeColorCount; ++ac)
            {
                storeColor(activeColors[ac]);
            }
        }
        for (const SolverConstraint &sc : overflowConstraints)
        {
            storeConstraintImpulses(sc);
        }
        PLATFORMATOR_WORK_ADD(solverColoredWideDispatches, 1);
        commitWritableBodies(solverTouchedWritableBodies, solverBodyStates);
        return true;
    }
#endif

    // General path (moving kinematic endpoints or non-AVX2 CPU).
    const size_t expectedContacts = island.contacts.size();
    solverPreStepJobs.clear();
    if (solverPreStepJobs.capacity() < expectedContacts)
    {
        solverPreStepJobs.reserve(expectedContacts);
    }

    const auto resolveBodySlot = [&](Rigidbody *rb) -> uint32_t {
        if (rb->isAwakeDynamic())
        {
            const size_t regIdx = rb->getPhysicsManagerIndex();
            if (regIdx < registeredBodies.size() && registeredBodies[regIdx] == rb)
            {
                return solverBodyLocalIndices[regIdx];
            }
            return UNMAPPED_SOLVER_BODY;
        }
        if (hasMovingKinematic && rb->isMovingKinematic())
        {
            return findOrAddKinematicBody(rb, solverBodyStates, solverBodyForces, solverKinematicBodies);
        }
        return 0;
    };

    std::array<size_t, IslandColorGraph::TOTAL_BUCKETS> colorBegin{};
    std::array<size_t, IslandColorGraph::TOTAL_BUCKETS> colorEnd{};
    for (size_t c = 0; c < IslandColorGraph::TOTAL_BUCKETS; ++c)
    {
        colorBegin[c] = solverPreStepJobs.size();
        for (ColliderPair *pair : graph.buckets[c])
        {
            Collision *collision = nullptr;
            Rigidbody *rbA = nullptr;
            Rigidbody *rbB = nullptr;
            if (!extractSolverPair(pair, collision, rbA, rbB))
            {
                continue;
            }
            const uint32_t idxA = resolveBodySlot(rbA);
            const uint32_t idxB = resolveBodySlot(rbB);
            if (idxA == UNMAPPED_SOLVER_BODY || idxB == UNMAPPED_SOLVER_BODY)
            {
                return false;
            }
            solverPreStepJobs.push_back(SolverPreStepJob{collision, rbA, rbB, idxA, idxB});
        }
        colorEnd[c] = solverPreStepJobs.size();
    }

    const size_t constraintCount = solverPreStepJobs.size();
    if (constraintCount == 0)
    {
        return true;
    }

    solverConstraints.resize(constraintCount);
    if (!allowParallel || constraintCount < PARALLEL_PRESTEP_THRESHOLD)
    {
        for (size_t i = 0; i < constraintCount; ++i)
        {
            prepareSolverConstraint(solverPreStepJobs[i], params, solverConstraints[i]);
        }
    }
    else
    {
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, constraintCount, 64),
            [&](const auto &range) {
                for (size_t i = range.begin(); i != range.end(); ++i)
                {
                    prepareSolverConstraint(solverPreStepJobs[i], params, solverConstraints[i]);
                }
            },
            tbb::static_partitioner(), context);
    }

    bool anyRestitution = false;
    for (size_t i = 0; i < constraintCount; ++i)
    {
        anyRestitution |= constraintHasActiveRestitution(solverConstraints[i], params.restitutionThreshold);
    }

    const size_t overflowBegin = colorBegin[IslandColorGraph::OVERFLOW_COLOR];
    const size_t overflowEnd = colorEnd[IslandColorGraph::OVERFLOW_COLOR];
    const size_t bodyCount = solverBodyStates.size();
    const float h = params.h;

    const auto runScalarColorPass = [&](auto &&fn, bool parallelBuckets) {
        for (uint8_t c = 0; c < IslandColorGraph::MAX_PARALLEL_COLORS; ++c)
        {
            const size_t begin = colorBegin[c];
            const size_t end = colorEnd[c];
            if (!parallelBuckets || !allowParallel || end - begin < PARALLEL_SOLVER_BATCH_SIZE)
            {
                for (size_t i = begin; i < end; ++i)
                {
                    fn(solverConstraints[i]);
                }
            }
            else
            {
                tbb::parallel_for(
                    tbb::blocked_range<size_t>(begin, end, 32),
                    [&](const auto &range) {
                        for (size_t i = range.begin(); i != range.end(); ++i)
                        {
                            fn(solverConstraints[i]);
                        }
                    },
                    tbb::static_partitioner(), context);
            }
        }
        for (size_t i = overflowBegin; i < overflowEnd; ++i)
        {
            fn(solverConstraints[i]);
        }
    };

    auto *bodyData = solverBodyStates.data();
    runSoftSteps(
        params, anyRestitution,
        [&] { integrateSolverVelocities(bodyData, solverBodyForces.data(), 1, bodyCount, h); },
        [&] {
            runScalarColorPass(
                [&](SolverConstraint &sc) { applyWarmStart(sc, bodyData[sc.bodyIndexA], bodyData[sc.bodyIndexB]); },
                true);
        },
        [&](auto useBias) {
            runScalarColorPass(
                [&](SolverConstraint &sc) {
                    solveConstraintSoft<decltype(useBias)::value>(sc, bodyData[sc.bodyIndexA], bodyData[sc.bodyIndexB],
                                                                  params);
                },
                true);
        },
        [&] { integrateSolverPositions(bodyData, 1, bodyCount, h); },
        [&] {
            runScalarColorPass(
                [&](SolverConstraint &sc) {
                    applyRestitutionSoft(sc, bodyData[sc.bodyIndexA], bodyData[sc.bodyIndexB], params);
                },
                false);
        });

    for (size_t i = 0; i < constraintCount; ++i)
    {
        storeConstraintImpulses(solverConstraints[i]);
    }
    commitWritableBodies(solverTouchedWritableBodies, solverBodyStates);
    return true;
}
