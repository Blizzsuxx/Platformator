#include "physics/physicsmanager.h"

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
    const bool useAVX2 = platformator_solver_simd::cpuSupportsAVX2();

    solverBodyStates.clear();
    solverBodyForces.clear();
    solverTouchedWritableBodies.clear();
    if (solverBodyStates.capacity() < island.bodies.size() + 1)
    {
        solverBodyStates.reserve(island.bodies.size() + 1);
    }
    if (solverBodyForces.capacity() < island.bodies.size() + 1)
    {
        solverBodyForces.reserve(island.bodies.size() + 1);
    }
    solverBodyStates.push_back(SolverBodyState{}); // slot 0: static/sleeping read-only
    solverBodyForces.push_back(SolverBodyForce{});

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
            const auto &v = rb->getVelocity();
            solverBodyStates.push_back(SolverBodyState{v.x(), v.y(), rb->getAngularVelocity(), rb->getInverseMass(),
                                                       0.0f, 0.0f, 0.0f, rb->getInverseMomentOfInertia()});
            solverBodyForces.push_back(makeBodyForce(rb));
            solverTouchedWritableBodies.push_back(WritableSolverBody{regIdx, mapped, rb});
        }
    }

    const bool hasMovingKinematic = !movingKinematicBodies.empty();
    tbb::task_group_context context(tbb::task_group_context::isolated);

#if PLATFORMATOR_HAS_RUNTIME_AVX2
    if (useAVX2 && !hasMovingKinematic)
    {
        using namespace platformator_solver_simd;
        auto *wideBuckets = colorWideBlocks.data();
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
        const auto packColorDirect = [&, wideBuckets, tailBuckets](uint8_t c) {
            const auto &pairs = graph.buckets[c];
            auto &wideOut = wideBuckets[c];
            auto &tailOut = tailBuckets[c];
            wideOut.clear();
            tailOut.clear();
            bool localRestitution = false;

            const auto emitBatch = [&](const SolverConstraint *batch, size_t count) {
                if (count == 0)
                {
                    return;
                }
                if (count < 4)
                {
                    for (size_t i = 0; i < count; ++i)
                    {
                        tailOut.push_back(batch[i]);
                    }
                    return;
                }
                wideOut.emplace_back();
                WideConstraint8 &wide = wideOut.back();
                wide.resetMasks();
                for (size_t lane = 0; lane < 8; ++lane)
                {
                    if (lane < count)
                    {
                        const SolverConstraint &sc = batch[lane];
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
            };

            if (pairs.size() >= 4 && wideOut.capacity() < (pairs.size() + 7) / 8 + 1)
            {
                wideOut.reserve((pairs.size() + 7) / 8 + 1);
            }
            SolverConstraint twoPointBatch[8];
            SolverConstraint onePointBatch[8];
            size_t twoPointCount = 0;
            size_t onePointCount = 0;
            SolverConstraint tmp;

            for (ColliderPair *pair : pairs)
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
                    return;
                }
                prepareSolverConstraint(SolverPreStepJob{collision, rbA, rbB, idxA, idxB}, params, tmp);
                localRestitution |= (tmp.solverRestitution > 0.0f);
                if (tmp.pointCount == 2)
                {
                    twoPointBatch[twoPointCount++] = tmp;
                    if (twoPointCount == 8)
                    {
                        emitBatch(twoPointBatch, 8);
                        twoPointCount = 0;
                    }
                }
                else if (tmp.pointCount == 1)
                {
                    onePointBatch[onePointCount++] = tmp;
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
                if (sc.solverRestitution > 0.0f)
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
                const size_t wideCount = wideBuckets[c].size();
                if (!allowParallel || wideCount < parallelWideThreshold)
                {
                    for (size_t w = 0; w < wideCount; ++w)
                    {
                        wideFn(wideData[w]);
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
                                wideFn(wideData[w]);
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

        for (size_t substep = 0; substep < params.subSteps; ++substep)
        {
            if (params.integrateForces && h > 0.0f)
            {
                runBodyRange([=](size_t begin, size_t end) {
                    for (size_t i = begin; i < end; ++i)
                    {
                        if (forceData[i].isDynamic)
                        {
                            bodyData[i].vx += forceData[i].accelX * h;
                            bodyData[i].vy += forceData[i].accelY * h;
                            bodyData[i].w += forceData[i].angAccel * h;
                        }
                    }
                });
            }

            if (substep == 0)
            {
                runWideColorPass(
                    [=](WideConstraint8 &wide) { applyWarmStartWide8AVX2(wide, bodyData); },
                    [=](SolverConstraint &sc) {
                        applyWarmStart(sc, bodyData[sc.bodyIndexA], bodyData[sc.bodyIndexB]);
                    });
            }

            runWideColorPass(
                [&](WideConstraint8 &wide) { solveWide8SoftAVX2<true>(wide, bodyData, params); },
                [&](SolverConstraint &sc) {
                    solveConstraintSoft(sc, bodyData[sc.bodyIndexA], bodyData[sc.bodyIndexB], params, true);
                });

            if (h > 0.0f)
            {
                runBodyRange([=](size_t begin, size_t end) {
                    for (size_t i = begin; i < end; ++i)
                    {
                        bodyData[i].dpX += bodyData[i].vx * h;
                        bodyData[i].dpY += bodyData[i].vy * h;
                        bodyData[i].dAngle += bodyData[i].w * h;
                    }
                });
            }

            runWideColorPass(
                [&](WideConstraint8 &wide) { solveWide8SoftAVX2<false>(wide, bodyData, params); },
                [&](SolverConstraint &sc) {
                    solveConstraintSoft(sc, bodyData[sc.bodyIndexA], bodyData[sc.bodyIndexB], params, false);
                });

            if (substep + 1 == params.subSteps && hasRestitution)
            {
                runWideColorPass(
                    [&](WideConstraint8 &wide) { applyRestitutionWide8AVX2(wide, bodyData, params); },
                    [&](SolverConstraint &sc) {
                        applyRestitutionSoft(sc, bodyData[sc.bodyIndexA], bodyData[sc.bodyIndexB], params);
                    });
            }
        }

        const auto storeColor = [wideBuckets, tailBuckets](uint8_t c) {
            for (const WideConstraint8 &wide : wideBuckets[c])
            {
                storeWideImpulses(wide);
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
        commitWritableBodies(solverTouchedWritableBodies, solverBodyStates, params.commitPositions);
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
            const uint32_t slot = static_cast<uint32_t>(solverBodyStates.size());
            const auto &v = rb->getVelocity();
            solverBodyStates.push_back(SolverBodyState{v.x(), v.y(), rb->getAngularVelocity(), 0.0f, 0.0f, 0.0f, 0.0f,
                                                       0.0f});
            solverBodyForces.push_back(SolverBodyForce{});
            return slot;
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
        anyRestitution |= (solverConstraints[i].solverRestitution > 0.0f);
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

    for (size_t substep = 0; substep < params.subSteps; ++substep)
    {
        if (params.integrateForces && h > 0.0f)
        {
            for (size_t i = 1; i < bodyCount; ++i)
            {
                if (solverBodyForces[i].isDynamic)
                {
                    solverBodyStates[i].vx += solverBodyForces[i].accelX * h;
                    solverBodyStates[i].vy += solverBodyForces[i].accelY * h;
                    solverBodyStates[i].w += solverBodyForces[i].angAccel * h;
                }
            }
        }
        if (substep == 0)
        {
            runScalarColorPass(
                [&](SolverConstraint &sc) {
                    applyWarmStart(sc, solverBodyStates[sc.bodyIndexA], solverBodyStates[sc.bodyIndexB]);
                },
                true);
        }

        runScalarColorPass(
            [&](SolverConstraint &sc) {
                solveConstraintSoft(sc, solverBodyStates[sc.bodyIndexA], solverBodyStates[sc.bodyIndexB], params, true);
            },
            true);

        if (h > 0.0f)
        {
            for (size_t i = 1; i < bodyCount; ++i)
            {
                solverBodyStates[i].dpX += solverBodyStates[i].vx * h;
                solverBodyStates[i].dpY += solverBodyStates[i].vy * h;
                solverBodyStates[i].dAngle += solverBodyStates[i].w * h;
            }
        }

        runScalarColorPass(
            [&](SolverConstraint &sc) {
                solveConstraintSoft(sc, solverBodyStates[sc.bodyIndexA], solverBodyStates[sc.bodyIndexB], params,
                                    false);
            },
            true);

        if (substep + 1 == params.subSteps && anyRestitution)
        {
            runScalarColorPass(
                [&](SolverConstraint &sc) {
                    applyRestitutionSoft(sc, solverBodyStates[sc.bodyIndexA], solverBodyStates[sc.bodyIndexB], params);
                },
                false);
        }
    }

    for (size_t i = 0; i < constraintCount; ++i)
    {
        storeConstraintImpulses(solverConstraints[i]);
    }
    commitWritableBodies(solverTouchedWritableBodies, solverBodyStates, params.commitPositions);
    return true;
}
