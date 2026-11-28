#include "physics/physicsmanager.h"

#include "benchmark/workprobe.h"
#include "physics/physicstaskcontext.h"

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

void PhysicsManager::populateColoredIslandBodies(const PersistentIsland &island)
{
    auto &cs = coloredScratch;
    cs.bodyStates.clear();
    cs.bodyForces.clear();
    cs.bodyPrep.clear();
    cs.kinematicBodies.clear();
    cs.writableBodies.clear();

    const size_t islandBodyCount = island.bodies.size();
    if (cs.bodyStates.capacity() < islandBodyCount + 1)
    {
        cs.bodyStates.reserve(islandBodyCount + 1);
        cs.bodyForces.reserve(islandBodyCount + 1);
        cs.bodyPrep.reserve(islandBodyCount + 1);
    }
    cs.bodyStates.push_back(SolverBodyState{}); // slot 0: static/sleeping read-only
    cs.bodyForces.push_back(SolverBodyForce{});
    cs.bodyPrep.push_back(BodyPrepCache{});

    cs.bodyLocalIndices.assign(islandBodyCount, UNMAPPED_SOLVER_BODY);

    for (size_t bi = 0; bi < islandBodyCount; ++bi)
    {
        Rigidbody *rb = island.bodies[bi];
        if (!rb->isAwakeDynamic())
        {
            continue;
        }
        const uint32_t mapped = static_cast<uint32_t>(cs.bodyStates.size());
        cs.bodyLocalIndices[bi] = mapped;
        cs.bodyStates.push_back(makeDynamicBodyState(rb));
        cs.bodyForces.push_back(makeBodyForce(rb));
        cs.bodyPrep.push_back(makeBodyPrepCache(rb));
        cs.writableBodies.push_back(WritableSolverBody{mapped, rb});
    }
}

bool PhysicsManager::solveColoredIslandWideAVX2(const PersistentIsland &island, const SoftStepParams &params,
                                                bool allowParallel)
{
#if PLATFORMATOR_HAS_RUNTIME_AVX2
    using namespace platformator_solver_simd;
    const IslandColorGraph &graph = *island.colorGraph;
    auto &cs = coloredScratch;
    const size_t islandBodyCount = island.bodies.size();

    auto *wideBuckets = cs.colorWideBlocks.data();
    auto *coldBuckets = cs.colorWideColdBlocks.data();
    auto *tailBuckets = cs.colorTailConstraints.data();

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
            if (rb->solverIslandId == island.id && rb->solverIslandBodyIndex < islandBodyCount)
            {
                return cs.bodyLocalIndices[rb->solverIslandBodyIndex];
            }
            return UNMAPPED_SOLVER_BODY;
        }
        return 0;
    };

    tbb::task_group_context context(tbb::task_group_context::isolated);
    std::atomic<bool> allMapped{true};
    std::atomic<uint32_t> restitutionColorMask{0};

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
                        cs.bodyStates.data(), cs.bodyForces.data(), cs.bodyPrep.data(), params);
                }
                else
                {
                    clearWideLane(wide, cold, lane);
                }
            }
            finalizeWideEffectiveMasses8AVX2(wide, cs.bodyStates.data());
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
                platformator_detail::prefetchColliderPair(pairs[i + 4]);
            }
            SolverContactPair contact;
            if (!extractSolverContactPair(pairs[i], contact))
            {
                continue;
            }
            const size_t pointCount = contact.collision->getContactPoints().count;
            if (pointCount == 0)
            {
                continue;
            }
            const uint32_t idxA = resolveBodySlotReadOnly(contact.refBody);
            const uint32_t idxB = resolveBodySlotReadOnly(contact.incBody);
            if (idxA == UNMAPPED_SOLVER_BODY || idxB == UNMAPPED_SOLVER_BODY)
            {
                allMapped.store(false, std::memory_order_relaxed);
                return;
            }
            const SolverPreStepJob job{contact.collision, contact.refBody, contact.incBody, idxA, idxB};
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
            restitutionColorMask.fetch_or(uint32_t{1} << c, std::memory_order_relaxed);
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

    cs.overflowConstraints.clear();
    const auto &overflowPairs = graph.buckets[IslandColorGraph::OVERFLOW_COLOR];
    if (!overflowPairs.empty() && allMapped.load(std::memory_order_relaxed))
    {
        cs.overflowConstraints.reserve(overflowPairs.size());
        for (ColliderPair *pair : overflowPairs)
        {
            SolverContactPair contact;
            if (!extractSolverContactPair(pair, contact))
            {
                continue;
            }
            const uint32_t idxA = resolveBodySlotReadOnly(contact.refBody);
            const uint32_t idxB = resolveBodySlotReadOnly(contact.incBody);
            if (idxA == UNMAPPED_SOLVER_BODY || idxB == UNMAPPED_SOLVER_BODY)
            {
                allMapped.store(false, std::memory_order_relaxed);
                break;
            }
            cs.overflowConstraints.emplace_back();
            SolverConstraint &sc = cs.overflowConstraints.back();
            prepareSolverConstraint(SolverPreStepJob{contact.collision, contact.refBody, contact.incBody, idxA, idxB},
                                    params, sc);
            if (constraintHasActiveRestitution(sc, params.restitutionThreshold))
            {
                restitutionColorMask.fetch_or(uint32_t{1} << IslandColorGraph::OVERFLOW_COLOR,
                                              std::memory_order_relaxed);
            }
        }
    }

    if (!allMapped.load(std::memory_order_relaxed))
    {
        return false;
    }

    SolverBodyState *bodyData = cs.bodyStates.data();
    const SolverBodyForce *forceData = cs.bodyForces.data();
    const size_t bodyCount = cs.bodyStates.size();
    const float h = params.h;
    const uint32_t restMask = restitutionColorMask.load(std::memory_order_relaxed);
    const bool hasRestitution = restMask != 0;
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

    const auto runWideColorPass = [&](uint32_t colorFilterMask, auto &&wideFn, auto &&scalarFn) {
        for (size_t ac = 0; ac < activeColorCount; ++ac)
        {
            const uint8_t c = activeColors[ac];
            if ((colorFilterMask & (uint32_t{1} << c)) == 0)
            {
                continue;
            }
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
        if ((colorFilterMask & (uint32_t{1} << IslandColorGraph::OVERFLOW_COLOR)) != 0)
        {
            for (SolverConstraint &sc : cs.overflowConstraints)
            {
                scalarFn(sc);
            }
        }
    };

    constexpr uint32_t allColorsMask = ~uint32_t{0};
    runSoftSteps(
        params, hasRestitution,
        [&] {
            runBodyRange([=](size_t begin, size_t end) {
                integrateSolverVelocities(bodyData, forceData, begin, end, h);
            });
        },
        [&] {
            runWideColorPass(
                allColorsMask,
                [=](WideConstraint8 &wide, const WideConstraint8Cold &) { applyWarmStartWide8AVX2(wide, bodyData); },
                [=](SolverConstraint &sc) { applyWarmStart(sc, bodyData[sc.bodyIndexA], bodyData[sc.bodyIndexB]); });
        },
        [&](auto useBias) {
            runWideColorPass(
                allColorsMask,
                [&](WideConstraint8 &wide, const WideConstraint8Cold &) {
                    solveWide8SoftAVX2<decltype(useBias)::value>(wide, bodyData, params);
                },
                [&](SolverConstraint &sc) {
                    solveConstraintSoft<decltype(useBias)::value>(sc, bodyData[sc.bodyIndexA], bodyData[sc.bodyIndexB],
                                                                  params);
                });
        },
        [&] {
            runBodyRange([=](size_t begin, size_t end) { integrateSolverPositions(bodyData, begin, end, h); });
        },
        [&] {
            runWideColorPass(
                restMask,
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
    for (const SolverConstraint &sc : cs.overflowConstraints)
    {
        storeConstraintImpulses(sc);
    }
    PLATFORMATOR_WORK_ADD(solverColoredWideDispatches, 1);
    commitWritableBodies(cs.writableBodies, cs.bodyStates);
    return true;
#else
    (void)island;
    (void)params;
    (void)allowParallel;
    return false;
#endif
}

bool PhysicsManager::solveColoredIslandScalar(const PersistentIsland &island, const SoftStepParams &params,
                                              bool allowParallel, bool hasMovingKinematic)
{
    const IslandColorGraph &graph = *island.colorGraph;
    auto &cs = coloredScratch;
    const size_t islandBodyCount = island.bodies.size();
    const size_t expectedContacts = island.contacts.size();

    cs.preStepJobs.clear();
    if (cs.preStepJobs.capacity() < expectedContacts)
    {
        cs.preStepJobs.reserve(expectedContacts);
    }

    const auto resolveBodySlot = [&](Rigidbody *rb) -> uint32_t {
        if (rb->isAwakeDynamic())
        {
            if (rb->solverIslandId == island.id && rb->solverIslandBodyIndex < islandBodyCount)
            {
                return cs.bodyLocalIndices[rb->solverIslandBodyIndex];
            }
            return UNMAPPED_SOLVER_BODY;
        }
        if (hasMovingKinematic && rb->isMovingKinematic())
        {
            return findOrAddKinematicBody(rb, cs.bodyStates, cs.bodyForces, cs.kinematicBodies);
        }
        return 0;
    };

    std::array<size_t, IslandColorGraph::TOTAL_BUCKETS> colorBegin{};
    std::array<size_t, IslandColorGraph::TOTAL_BUCKETS> colorEnd{};
    for (size_t c = 0; c < IslandColorGraph::TOTAL_BUCKETS; ++c)
    {
        colorBegin[c] = cs.preStepJobs.size();
        for (ColliderPair *pair : graph.buckets[c])
        {
            SolverContactPair contact;
            if (!extractSolverContactPair(pair, contact))
            {
                continue;
            }
            const uint32_t idxA = resolveBodySlot(contact.refBody);
            const uint32_t idxB = resolveBodySlot(contact.incBody);
            if (idxA == UNMAPPED_SOLVER_BODY || idxB == UNMAPPED_SOLVER_BODY)
            {
                return false;
            }
            cs.preStepJobs.push_back(SolverPreStepJob{contact.collision, contact.refBody, contact.incBody, idxA, idxB});
        }
        colorEnd[c] = cs.preStepJobs.size();
    }

    const size_t constraintCount = cs.preStepJobs.size();
    if (constraintCount == 0)
    {
        return true;
    }

    tbb::task_group_context context(tbb::task_group_context::isolated);
    cs.constraints.resize(constraintCount);
    if (!allowParallel || constraintCount < PARALLEL_PRESTEP_THRESHOLD)
    {
        for (size_t i = 0; i < constraintCount; ++i)
        {
            prepareSolverConstraint(cs.preStepJobs[i], params, cs.constraints[i]);
        }
    }
    else
    {
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, constraintCount, 64),
            [&](const auto &range) {
                for (size_t i = range.begin(); i != range.end(); ++i)
                {
                    prepareSolverConstraint(cs.preStepJobs[i], params, cs.constraints[i]);
                }
            },
            tbb::static_partitioner(), context);
    }

    bool anyRestitution = false;
    for (size_t i = 0; i < constraintCount; ++i)
    {
        anyRestitution |= constraintHasActiveRestitution(cs.constraints[i], params.restitutionThreshold);
    }

    const size_t overflowBegin = colorBegin[IslandColorGraph::OVERFLOW_COLOR];
    const size_t overflowEnd = colorEnd[IslandColorGraph::OVERFLOW_COLOR];
    const size_t bodyCount = cs.bodyStates.size();
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
                    fn(cs.constraints[i]);
                }
            }
            else
            {
                tbb::parallel_for(
                    tbb::blocked_range<size_t>(begin, end, 32),
                    [&](const auto &range) {
                        for (size_t i = range.begin(); i != range.end(); ++i)
                        {
                            fn(cs.constraints[i]);
                        }
                    },
                    tbb::static_partitioner(), context);
            }
        }
        for (size_t i = overflowBegin; i < overflowEnd; ++i)
        {
            fn(cs.constraints[i]);
        }
    };

    auto *bodyData = cs.bodyStates.data();
    runSoftSteps(
        params, anyRestitution,
        [&] { integrateSolverVelocities(bodyData, cs.bodyForces.data(), 1, bodyCount, h); },
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
        storeConstraintImpulses(cs.constraints[i]);
    }
    commitWritableBodies(cs.writableBodies, cs.bodyStates);
    return true;
}

bool PhysicsManager::solveColoredIsland(PersistentIsland &island, const SoftStepParams &params, bool allowParallel)
{
    ensureIslandColorGraph(island);
    populateColoredIslandBodies(island);

    const bool hasMovingKinematic = !movingKinematicBodies.empty();
#if PLATFORMATOR_HAS_RUNTIME_AVX2
    const bool wideEligible =
        platformator_solver_simd::cpuSupportsAVX2() &&
        (!hasMovingKinematic || std::ranges::none_of(island.contacts, [&](const ColliderPair *pair) {
             PLATFORMATOR_WORK_ADD(solverColoredEligibilityContactVisits, 1);
             const auto [rbA, rbB] = pairRigidbodies(pair);
             return (rbA && rbA->isMovingKinematic()) || (rbB && rbB->isMovingKinematic());
         }));
    if (wideEligible)
    {
        return solveColoredIslandWideAVX2(island, params, allowParallel);
    }
#endif
    return solveColoredIslandScalar(island, params, allowParallel, hasMovingKinematic);
}
