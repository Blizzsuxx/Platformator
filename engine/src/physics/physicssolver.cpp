#include "physics/physicsmanager.h"
#include "physics/physicssolver_simd.h"

#include "benchmark/benchmark.h"
#include "benchmark/workprobe.h"
#include "core/constants.h"
#include "core/gamemanager.h"
#include "core/runtimeaccess.h"
#include "oneapi/tbb.h"
#include "core/membership.h"

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

inline void applyLocalImpulse(Eigen::Vector2f &velocity, float &angularVelocity, float inverseMass,
                              float inverseMomentOfInertia, const Eigen::Vector2f &impulse,
                              const Eigen::Vector2f &arm) noexcept
{
    velocity += impulse * inverseMass;
    angularVelocity += inverseMomentOfInertia * (arm.x() * impulse.y() - arm.y() * impulse.x());
}
} // namespace

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
