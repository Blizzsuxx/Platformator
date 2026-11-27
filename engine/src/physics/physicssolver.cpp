#include "physics/physicsmanager.h"
#include "physics/physicssolver_simd.h"

#include "benchmark/benchmark.h"
#include "benchmark/workprobe.h"
#include "core/constants.h"
#include "core/gamemanager.h"
#include "core/membership.h"
#include "core/runtimeaccess.h"
#include "oneapi/tbb.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>

namespace
{
constexpr size_t parallelSolverBatchSize = 128;
constexpr size_t parallelPreStepThreshold = 256;
constexpr size_t parallelBodySubstepThreshold = 512;
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
// Soft Step Parameters, Entry Points & Body Integration Helpers
// ============================================================================

PhysicsManager::SoftStepParams PhysicsManager::makeSoftStepParams(double timeDelta, bool integrateForces,
                                                                  bool commitPositions) const noexcept
{
    SoftStepParams params;
    params.subSteps = COLLISION_SOLVER_SUBSTEPS;
    params.dt = timeDelta > 0.0 ? static_cast<float>(timeDelta) : 0.0f;
    params.h = params.dt > 0.0f ? params.dt / static_cast<float>(params.subSteps) : 0.0f;
    params.invH = params.h > 0.0f ? 1.0f / params.h : 0.0f;

    if (params.h > 0.0f)
    {
        const float contactHertz = std::min(CONTACT_HERTZ, 0.25f * params.invH);
        const float omega = 2.0f * static_cast<float>(std::numbers::pi) * contactHertz;
        const float a1 = 2.0f * CONTACT_DAMPING_RATIO + params.h * omega;
        const float a2 = params.h * omega * a1;
        params.biasRate = a1 > 0.0f ? (omega / a1) : 0.0f;
        params.impulseScale = 1.0f / (1.0f + a2);
        params.massScale = a2 / (1.0f + a2);
    }
    else
    {
        params.biasRate = 0.0f;
        params.impulseScale = 0.0f;
        params.massScale = 1.0f;
    }

    params.negMaxPushSpeed = -std::max(30.0f, gravityVector.norm() * 0.35f);
    params.slop = COLLISION_ALLOWED_PENETRATION;
    params.restitutionThreshold = RESTITUTION_VELOCITY_THRESHOLD;
    params.integrateForces = integrateForces;
    params.commitPositions = commitPositions;
    return params;
}

PhysicsManager::SolverBodyForce PhysicsManager::makeBodyForce(const Rigidbody *rb) const noexcept
{
    if (rb == nullptr || rb->getBodyType() != DYNAMIC || rb->getIsSleeping())
    {
        return SolverBodyForce{};
    }
    const Eigen::Vector2f accel =
        rb->getForce() * rb->getInverseMass() + (rb->getGravity() ? gravityVector : Eigen::Vector2f::Zero());
    const float angAccel = rb->getTorque() * rb->getInverseMomentOfInertia();
    return SolverBodyForce{accel.x(), accel.y(), angAccel, 1};
}

void PhysicsManager::stepDynamics(double timeDelta)
{
    {
        PLATFORMATOR_BENCH_SCOPE(ResolveCollisions);

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
        reconcileChangedBodies();

        PLATFORMATOR_WORK_ADD(forceBodies, awakeDynamicBodies.size());

        const size_t bodyCount = registeredBodies.size();
        if (solverBodyDeltas.size() < bodyCount)
        {
            solverBodyDeltas.resize(bodyCount);
        }
        solverBodyIntegrated.assign(bodyCount, 0);

        const SoftStepParams params = makeSoftStepParams(timeDelta, /*integrateForces=*/true, /*commitPositions=*/true);
        solveConstraintIterations(params);
    }

    commitMovedBodies(timeDelta, /*useSolverDeltas=*/true);

    reconcileChangedBodies();
    updateSleepingStates(timeDelta);

#if PLATFORMATOR_ENABLE_BENCHMARKS
    PLATFORMATOR_BENCH_SET_COUNTER(AwakeDynamicBodyCount, countAwakeDynamicBodies());
#endif
}

void PhysicsManager::resolveCollisions(double timeDelta)
{
    PLATFORMATOR_BENCH_SCOPE(ResolveCollisions);

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

    const SoftStepParams params = makeSoftStepParams(timeDelta, /*integrateForces=*/false, /*commitPositions=*/false);
    solveConstraintIterations(params);

    reconcileChangedBodies();
    updateSleepingStates(timeDelta);

#if PLATFORMATOR_ENABLE_BENCHMARKS
    PLATFORMATOR_BENCH_SET_COUNTER(AwakeDynamicBodyCount, countAwakeDynamicBodies());
#endif
}

// ============================================================================
// Scalar Single-Island Soft Step Solver
// ============================================================================

void PhysicsManager::solvePersistentIsland(const PersistentIsland &island, const SoftStepParams &params,
                                           IslandSolverScratch &scratch)
{
    auto &local = scratch.constraints;
    auto &bodies = scratch.bodies;
    auto &bodyForces = scratch.bodyForces;
    auto &writable = scratch.writableBodies;
    local.clear();
    if (local.capacity() < island.contacts.size())
    {
        local.reserve(island.contacts.size());
    }
    bodies.clear();
    bodyForces.clear();
    bodies.push_back(SolverBodyState{}); // slot 0: shared zero-velocity read-only body (static/sleeping)
    bodyForces.push_back(SolverBodyForce{});
    writable.clear();

    bool allIndexed = true;
    const auto mapBody = [&](Rigidbody *rb) -> uint32_t {
        if (rb->getBodyType() != DYNAMIC || rb->getIsSleeping())
        {
            if (rb->getBodyType() == KINEMATIC && (!rb->getVelocity().isZero(0) || rb->getAngularVelocity() != 0.0f))
            {
                const uint32_t localIdx = static_cast<uint32_t>(bodies.size());
                bodies.push_back(
                    SolverBodyState{rb->getVelocity(), rb->getAngularVelocity(), Eigen::Vector2f::Zero(), 0.0f, 0.0f, 0.0f});
                bodyForces.push_back(SolverBodyForce{0.0f, 0.0f, 0.0f, 0});
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
            bodies.push_back(SolverBodyState{rb->getVelocity(), rb->getAngularVelocity(), Eigen::Vector2f::Zero(), 0.0f,
                                             rb->getInverseMass(), rb->getInverseMomentOfInertia()});
            bodyForces.push_back(makeBodyForce(rb));
            writable.emplace_back(regIdx, rb);
        }
        return mapped;
    };

    bool anyRestitution = false;
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
        prepareSolverConstraint(SolverPreStepJob{collision, rbA, rbB}, params, c);
        anyRestitution |= (c.solverRestitution > 0.0f);
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
        const size_t bodyCount = bodies.size();
        const float h = params.h;

        for (size_t substep = 0; substep < params.subSteps; ++substep)
        {
            if (params.integrateForces && h > 0.0f)
            {
                for (size_t i = 1; i < bodyCount; ++i)
                {
                    if (bodyForces[i].isDynamic)
                    {
                        bodies[i].velocity.x() += bodyForces[i].accelX * h;
                        bodies[i].velocity.y() += bodyForces[i].accelY * h;
                        bodies[i].angularVelocity += bodyForces[i].angAccel * h;
                    }
                }
            }
            if (substep == 0)
            {
                for (const SolverConstraint &constraint : local)
                {
                    applyWarmStart(constraint, bodies[constraint.bodyIndexA], bodies[constraint.bodyIndexB]);
                }
            }
            for (SolverConstraint &constraint : local)
            {
                solveConstraintSoft(constraint, bodies[constraint.bodyIndexA], bodies[constraint.bodyIndexB], params,
                                    true);
            }
            if (h > 0.0f)
            {
                for (size_t i = 1; i < bodyCount; ++i)
                {
                    bodies[i].deltaPosition += bodies[i].velocity * h;
                    bodies[i].deltaAngle += bodies[i].angularVelocity * h;
                }
            }
            for (SolverConstraint &constraint : local)
            {
                solveConstraintSoft(constraint, bodies[constraint.bodyIndexA], bodies[constraint.bodyIndexB], params,
                                    false);
            }
            if (substep + 1 == params.subSteps && anyRestitution)
            {
                for (SolverConstraint &constraint : local)
                {
                    applyRestitutionSoft(constraint, bodies[constraint.bodyIndexA], bodies[constraint.bodyIndexB],
                                         params);
                }
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
            if (params.commitPositions)
            {
                solverBodyDeltas[regIdx] =
                    SolverBodyDelta{state.deltaPosition.x(), state.deltaPosition.y(), state.deltaAngle};
                solverBodyIntegrated[regIdx] = 1;
            }
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
    for (size_t substep = 0; substep < params.subSteps; ++substep)
    {
        for (bool useBias : {true, false})
        {
            for (SolverConstraint &constraint : local)
            {
                const bool dynA = constraint.rbA->getBodyType() == DYNAMIC && !constraint.rbA->getIsSleeping();
                const bool dynB = constraint.rbB->getBodyType() == DYNAMIC && !constraint.rbB->getIsSleeping();
                SolverBodyState bodyA{constraint.rbA->getVelocity(),
                                      constraint.rbA->getAngularVelocity(),
                                      Eigen::Vector2f::Zero(),
                                      0.0f,
                                      dynA ? constraint.rbA->getInverseMass() : 0.0f,
                                      dynA ? constraint.rbA->getInverseMomentOfInertia() : 0.0f};
                SolverBodyState bodyB{constraint.rbB->getVelocity(),
                                      constraint.rbB->getAngularVelocity(),
                                      Eigen::Vector2f::Zero(),
                                      0.0f,
                                      dynB ? constraint.rbB->getInverseMass() : 0.0f,
                                      dynB ? constraint.rbB->getInverseMomentOfInertia() : 0.0f};
                solveConstraintSoft(constraint, bodyA, bodyB, params, useBias);
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
    }
    for (const SolverConstraint &constraint : local)
    {
        storeConstraintImpulses(constraint);
    }
}

// ============================================================================
// 8-Wide AVX2 Independent Island-Group Soft Step Solver (Zero-Barrier Substeps)
// ============================================================================

bool PhysicsManager::solvePersistentIslandGroup8(const int32_t *islandIds, size_t groupSize,
                                                 const SoftStepParams &params, IslandSolverScratch &scratch)
{
#if PLATFORMATOR_HAS_RUNTIME_AVX2
    using namespace platformator_solver_simd;
    auto &wideBodies = scratch.wideBodies;
    auto &bodyForces = scratch.bodyForces;
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
    bodyForces.clear();
    wideBodies.push_back(WideBodyState{}); // slot 0: shared static/sleeping zero-velocity body
    bodyForces.push_back(SolverBodyForce{});
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
                wideBodies.push_back(WideBodyState{v.x(), v.y(), rb->getAngularVelocity(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f});
                bodyForces.push_back(SolverBodyForce{0.0f, 0.0f, 0.0f, 0});
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
            wideBodies.push_back(WideBodyState{v.x(), v.y(), rb->getAngularVelocity(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f});
            bodyForces.push_back(makeBodyForce(rb));
            writable.emplace_back(regIdx, rb);
        }
        return mapped;
    };

    thread_local std::vector<WideConstraint8> wideSteps;
    wideSteps.resize(maxPossibleSteps);
    for (size_t step = 0; step < maxPossibleSteps; ++step)
    {
        wideSteps[step].twoPointMask = 0;
        wideSteps[step].writeMaskA = 0;
        wideSteps[step].writeMaskB = 0;
        wideSteps[step].restitutionMask = 0;
    }

    size_t laneCounts[8] = {};
    size_t maxLaneCount = 0;
    bool anyRestitution = false;
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
            prepareSolverConstraint(SolverPreStepJob{collision, rbA, rbB}, params, tmpConstraint);
            float invMA = 0.0f, invIA = 0.0f, invMB = 0.0f, invIB = 0.0f;
            tmpConstraint.bodyIndexA = mapBody(rbA, invMA, invIA);
            tmpConstraint.bodyIndexB = mapBody(rbB, invMB, invIB);
            if (!allIndexed)
            {
                break;
            }
            packWideLane(wideSteps[step], lane, &tmpConstraint, invMA, invIA, invMB, invIB);
            anyRestitution |= (tmpConstraint.solverRestitution > 0.0f);
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

    WideBodyState *bodyData = wideBodies.data();
    const size_t bodyCount = wideBodies.size();
    const float h = params.h;

    for (size_t substep = 0; substep < params.subSteps; ++substep)
    {
        if (params.integrateForces && h > 0.0f)
        {
            for (size_t i = 1; i < bodyCount; ++i)
            {
                if (bodyForces[i].isDynamic)
                {
                    bodyData[i].vx += bodyForces[i].accelX * h;
                    bodyData[i].vy += bodyForces[i].accelY * h;
                    bodyData[i].w += bodyForces[i].angAccel * h;
                }
            }
        }
        if (substep == 0)
        {
            for (size_t step = 0; step < maxLaneCount; ++step)
            {
                applyWarmStartWide8AVX2(wideSteps[step], bodyData);
            }
        }
        for (size_t step = 0; step < maxLaneCount; ++step)
        {
            solveWide8SoftAVX2<true>(wideSteps[step], bodyData, params);
        }
        if (h > 0.0f)
        {
            for (size_t i = 1; i < bodyCount; ++i)
            {
                bodyData[i].dpX += bodyData[i].vx * h;
                bodyData[i].dpY += bodyData[i].vy * h;
                bodyData[i].dAngle += bodyData[i].w * h;
            }
        }
        for (size_t step = 0; step < maxLaneCount; ++step)
        {
            solveWide8SoftAVX2<false>(wideSteps[step], bodyData, params);
        }
        if (substep + 1 == params.subSteps && anyRestitution)
        {
            for (size_t step = 0; step < maxLaneCount; ++step)
            {
                applyRestitutionWide8AVX2(wideSteps[step], bodyData, params);
            }
        }
    }

    for (size_t step = 0; step < maxLaneCount; ++step)
    {
        storeWideImpulses(wideSteps[step]);
    }

    for (const auto &[regIdx, rb] : writable)
    {
        const uint32_t localIdx = solverBodyLocalIndices[regIdx];
        const WideBodyState &state = wideBodies[localIdx];
        rb->velocity.x() = state.vx;
        rb->velocity.y() = state.vy;
        rb->angularVelocity = state.w;
        if (params.commitPositions)
        {
            solverBodyDeltas[regIdx] = SolverBodyDelta{state.dpX, state.dpY, state.dAngle};
            solverBodyIntegrated[regIdx] = 1;
        }
        solverBodyLocalIndices[regIdx] = UNMAPPED_SOLVER_BODY;
    }
    return true;
#else
    (void)islandIds;
    (void)groupSize;
    (void)params;
    (void)scratch;
    return false;
#endif
}

// ============================================================================
// Island Work-Unit Bucketing & Execution
// ============================================================================

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
        islandWorkUnits.push_back(IslandWorkUnit{static_cast<uint32_t>(idx), 1, static_cast<uint32_t>(c)});
        ++idx;
    }
}

void PhysicsManager::executeIslandWorkUnits(size_t beginUnit, size_t endUnit, const SoftStepParams &params,
                                            IslandSolverScratch &scratch)
{
    for (size_t u = beginUnit; u < endUnit; ++u)
    {
        const IslandWorkUnit &unit = islandWorkUnits[u];
        if (unit.count > 1)
        {
            if (solvePersistentIslandGroup8(bucketedIslandIds.data() + unit.begin, unit.count, params, scratch))
            {
                continue;
            }
        }
        for (size_t k = 0; k < unit.count; ++k)
        {
            const int32_t islandId = bucketedIslandIds[unit.begin + k];
            solvePersistentIsland(persistentIslands[static_cast<size_t>(islandId)], params, scratch);
        }
    }
}

// ============================================================================
// Graph-Colored Large-Island Soft Step Solver (Deterministic w=1 == w=N)
// ============================================================================

bool PhysicsManager::solveColoredIsland(PersistentIsland &island, const SoftStepParams &params, bool allowParallel)
{
    ensureIslandColorGraph(island);
    const IslandColorGraph &graph = *island.colorGraph;
    const bool useAVX2 = platformator_solver_simd::cpuSupportsAVX2();

    solverBodyStates.clear();
    solverWideBodyStates.clear();
    solverBodyForces.clear();
    solverTouchedWritableBodies.clear();
    if (solverBodyStates.capacity() < island.bodies.size() + 1)
    {
        solverBodyStates.reserve(island.bodies.size() + 1);
    }
    if (useAVX2 && solverWideBodyStates.capacity() < island.bodies.size() + 1)
    {
        solverWideBodyStates.reserve(island.bodies.size() + 1);
    }
    if (solverBodyForces.capacity() < island.bodies.size() + 1)
    {
        solverBodyForces.reserve(island.bodies.size() + 1);
    }
    solverBodyStates.push_back(SolverBodyState{}); // slot 0: static/sleeping read-only
    if (useAVX2)
    {
        solverWideBodyStates.push_back(WideBodyState{});
    }
    solverBodyForces.push_back(SolverBodyForce{});

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
            solverBodyStates.push_back(SolverBodyState{v, w, Eigen::Vector2f::Zero(), 0.0f, rb->getInverseMass(),
                                                       rb->getInverseMomentOfInertia()});
            if (useAVX2)
            {
                solverWideBodyStates.push_back(WideBodyState{v.x(), v.y(), w, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f});
            }
            solverBodyForces.push_back(makeBodyForce(rb));
            solverTouchedWritableBodies.emplace_back(regIdx, rb);
        }
    }

    const bool hasMovingKinematic = !movingKinematicBodies.empty();

#if PLATFORMATOR_HAS_RUNTIME_AVX2
    if (useAVX2 && !hasMovingKinematic)
    {
        using namespace platformator_solver_simd;
        thread_local std::array<std::vector<WideConstraint8>, IslandColorGraph::MAX_PARALLEL_COLORS> colorWideBlocks;
        thread_local std::array<std::vector<SolverConstraint>, IslandColorGraph::MAX_PARALLEL_COLORS> colorTailConstraints;
        thread_local std::vector<SolverConstraint> overflowConstraints;
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
            if (rb->getBodyType() == DYNAMIC && !rb->getIsSleeping())
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
                wide.twoPointMask = 0;
                wide.writeMaskA = 0;
                wide.writeMaskB = 0;
                wide.restitutionMask = 0;
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

            if (pairs.size() < 4)
            {
                for (const ColliderPair *pair : pairs)
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
                    const uint32_t idxA = resolveBodySlotReadOnly(rbA);
                    const uint32_t idxB = resolveBodySlotReadOnly(rbB);
                    if (idxA == UNMAPPED_SOLVER_BODY || idxB == UNMAPPED_SOLVER_BODY)
                    {
                        allMapped.store(false, std::memory_order_relaxed);
                        return;
                    }
                    tailOut.emplace_back();
                    SolverConstraint &sc = tailOut.back();
                    prepareSolverConstraint(SolverPreStepJob{collision, rbA, rbB}, params, sc);
                    sc.bodyIndexA = idxA;
                    sc.bodyIndexB = idxB;
                    localRestitution |= (sc.solverRestitution > 0.0f);
                }
                if (localRestitution)
                {
                    anyRestitution.store(true, std::memory_order_relaxed);
                }
                return;
            }

            if (wideOut.capacity() < (pairs.size() + 7) / 8 + 1)
            {
                wideOut.reserve((pairs.size() + 7) / 8 + 1);
            }
            SolverConstraint twoPointBatch[8];
            SolverConstraint onePointBatch[8];
            size_t twoPointCount = 0;
            size_t onePointCount = 0;
            SolverConstraint tmp;

            for (const ColliderPair *pair : pairs)
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
                const uint32_t idxA = resolveBodySlotReadOnly(rbA);
                const uint32_t idxB = resolveBodySlotReadOnly(rbB);
                if (idxA == UNMAPPED_SOLVER_BODY || idxB == UNMAPPED_SOLVER_BODY)
                {
                    allMapped.store(false, std::memory_order_relaxed);
                    return;
                }
                prepareSolverConstraint(SolverPreStepJob{collision, rbA, rbB}, params, tmp);
                tmp.bodyIndexA = idxA;
                tmp.bodyIndexB = idxB;
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

        tbb::task_group_context context(tbb::task_group_context::isolated);
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
            for (const ColliderPair *pair : overflowPairs)
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
                const uint32_t idxA = resolveBodySlotReadOnly(rbA);
                const uint32_t idxB = resolveBodySlotReadOnly(rbB);
                if (idxA == UNMAPPED_SOLVER_BODY || idxB == UNMAPPED_SOLVER_BODY)
                {
                    allMapped.store(false, std::memory_order_relaxed);
                    break;
                }
                overflowConstraints.emplace_back();
                SolverConstraint &sc = overflowConstraints.back();
                prepareSolverConstraint(SolverPreStepJob{collision, rbA, rbB}, params, sc);
                sc.bodyIndexA = idxA;
                sc.bodyIndexB = idxB;
                if (sc.solverRestitution > 0.0f)
                {
                    anyRestitution.store(true, std::memory_order_relaxed);
                }
            }
        }

        if (!allMapped.load(std::memory_order_relaxed))
        {
            for (const auto &[regIdx, rb] : solverTouchedWritableBodies)
            {
                (void)rb;
                solverBodyLocalIndices[regIdx] = UNMAPPED_SOLVER_BODY;
            }
            return false;
        }

        WideBodyState *bodyData = solverWideBodyStates.data();
        const SolverBodyForce *forceData = solverBodyForces.data();
        const size_t bodyCount = solverWideBodyStates.size();
        const float h = params.h;
        const bool hasRestitution = anyRestitution.load(std::memory_order_relaxed);
        constexpr size_t parallelWideThreshold = 32;

        const auto loadTailBody = [&](uint32_t idx) -> SolverBodyState {
            const WideBodyState &v = bodyData[idx];
            return SolverBodyState{{v.vx, v.vy},
                                   v.w,
                                   {v.dpX, v.dpY},
                                   v.dAngle,
                                   solverBodyStates[idx].inverseMass,
                                   solverBodyStates[idx].inverseMomentOfInertia};
        };
        const auto storeTailBody = [&](uint32_t idx, const SolverBodyState &s) {
            bodyData[idx].vx = s.velocity.x();
            bodyData[idx].vy = s.velocity.y();
            bodyData[idx].w = s.angularVelocity;
        };

        const auto runTailWarmStart = [&](const SolverConstraint &sc) {
            SolverBodyState sA = loadTailBody(sc.bodyIndexA);
            SolverBodyState sB = loadTailBody(sc.bodyIndexB);
            applyWarmStart(sc, sA, sB);
            storeTailBody(sc.bodyIndexA, sA);
            storeTailBody(sc.bodyIndexB, sB);
        };
        const auto runTailSolve = [&](SolverConstraint &sc, bool useBias) {
            SolverBodyState sA = loadTailBody(sc.bodyIndexA);
            SolverBodyState sB = loadTailBody(sc.bodyIndexB);
            solveConstraintSoft(sc, sA, sB, params, useBias);
            storeTailBody(sc.bodyIndexA, sA);
            storeTailBody(sc.bodyIndexB, sB);
        };
        const auto runTailRestitution = [&](SolverConstraint &sc) {
            if (sc.solverRestitution <= 0.0f)
            {
                return;
            }
            SolverBodyState sA = loadTailBody(sc.bodyIndexA);
            SolverBodyState sB = loadTailBody(sc.bodyIndexB);
            applyRestitutionSoft(sc, sA, sB, params);
            storeTailBody(sc.bodyIndexA, sA);
            storeTailBody(sc.bodyIndexB, sB);
        };

        for (size_t substep = 0; substep < params.subSteps; ++substep)
        {
            if (params.integrateForces && h > 0.0f)
            {
                if (!allowParallel || bodyCount < parallelBodySubstepThreshold)
                {
                    for (size_t i = 1; i < bodyCount; ++i)
                    {
                        if (forceData[i].isDynamic)
                        {
                            bodyData[i].vx += forceData[i].accelX * h;
                            bodyData[i].vy += forceData[i].accelY * h;
                            bodyData[i].w += forceData[i].angAccel * h;
                        }
                    }
                }
                else
                {
                    tbb::parallel_for(
                        tbb::blocked_range<size_t>(1, bodyCount, 256),
                        [=](const auto &range) {
                            for (size_t i = range.begin(); i != range.end(); ++i)
                            {
                                if (forceData[i].isDynamic)
                                {
                                    bodyData[i].vx += forceData[i].accelX * h;
                                    bodyData[i].vy += forceData[i].accelY * h;
                                    bodyData[i].w += forceData[i].angAccel * h;
                                }
                            }
                        },
                        tbb::static_partitioner(), context);
                }
            }

            if (substep == 0)
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
                            applyWarmStartWide8AVX2(wideData[w], bodyData);
                        }
                    }
                    else
                    {
                        const size_t grain = std::max<size_t>(8, wideCount / 16);
                        tbb::parallel_for(
                            tbb::blocked_range<size_t>(0, wideCount, grain),
                            [=](const auto &range) {
                                for (size_t w = range.begin(); w != range.end(); ++w)
                                {
                                    applyWarmStartWide8AVX2(wideData[w], bodyData);
                                }
                            },
                            tbb::static_partitioner(), context);
                    }
                    for (const SolverConstraint &sc : colorTailConstraints[c])
                    {
                        runTailWarmStart(sc);
                    }
                }
                for (const SolverConstraint &sc : overflowConstraints)
                {
                    runTailWarmStart(sc);
                }
            }

            // Velocity solve pass (with soft bias).
            for (size_t ac = 0; ac < activeColorCount; ++ac)
            {
                const uint8_t c = activeColors[ac];
                WideConstraint8 *wideData = colorWideBlocks[c].data();
                const size_t wideCount = colorWideBlocks[c].size();
                if (!allowParallel || wideCount < parallelWideThreshold)
                {
                    for (size_t w = 0; w < wideCount; ++w)
                    {
                        solveWide8SoftAVX2<true>(wideData[w], bodyData, params);
                    }
                }
                else
                {
                    const size_t grain = std::max<size_t>(8, wideCount / 16);
                    tbb::parallel_for(
                        tbb::blocked_range<size_t>(0, wideCount, grain),
                        [=](const auto &range) {
                            for (size_t w = range.begin(); w != range.end(); ++w)
                            {
                                solveWide8SoftAVX2<true>(wideData[w], bodyData, params);
                            }
                        },
                        tbb::static_partitioner(), context);
                }
                for (SolverConstraint &sc : colorTailConstraints[c])
                {
                    runTailSolve(sc, true);
                }
            }
            for (SolverConstraint &sc : overflowConstraints)
            {
                runTailSolve(sc, true);
            }

            // Integrate delta positions for substep h.
            if (h > 0.0f)
            {
                if (!allowParallel || bodyCount < parallelBodySubstepThreshold)
                {
                    for (size_t i = 1; i < bodyCount; ++i)
                    {
                        bodyData[i].dpX += bodyData[i].vx * h;
                        bodyData[i].dpY += bodyData[i].vy * h;
                        bodyData[i].dAngle += bodyData[i].w * h;
                    }
                }
                else
                {
                    tbb::parallel_for(
                        tbb::blocked_range<size_t>(1, bodyCount, 256),
                        [=](const auto &range) {
                            for (size_t i = range.begin(); i != range.end(); ++i)
                            {
                                bodyData[i].dpX += bodyData[i].vx * h;
                                bodyData[i].dpY += bodyData[i].vy * h;
                                bodyData[i].dAngle += bodyData[i].w * h;
                            }
                        },
                        tbb::static_partitioner(), context);
                }
            }

            // Relaxation pass (zero bias).
            for (size_t ac = 0; ac < activeColorCount; ++ac)
            {
                const uint8_t c = activeColors[ac];
                WideConstraint8 *wideData = colorWideBlocks[c].data();
                const size_t wideCount = colorWideBlocks[c].size();
                if (!allowParallel || wideCount < parallelWideThreshold)
                {
                    for (size_t w = 0; w < wideCount; ++w)
                    {
                        solveWide8SoftAVX2<false>(wideData[w], bodyData, params);
                    }
                }
                else
                {
                    const size_t grain = std::max<size_t>(8, wideCount / 16);
                    tbb::parallel_for(
                        tbb::blocked_range<size_t>(0, wideCount, grain),
                        [=](const auto &range) {
                            for (size_t w = range.begin(); w != range.end(); ++w)
                            {
                                solveWide8SoftAVX2<false>(wideData[w], bodyData, params);
                            }
                        },
                        tbb::static_partitioner(), context);
                }
                for (SolverConstraint &sc : colorTailConstraints[c])
                {
                    runTailSolve(sc, false);
                }
            }
            for (SolverConstraint &sc : overflowConstraints)
            {
                runTailSolve(sc, false);
            }

            // Restitution (after final substep relaxation, skipped completely when restitution == 0).
            if (substep + 1 == params.subSteps && hasRestitution)
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
                            applyRestitutionWide8AVX2(wideData[w], bodyData, params);
                        }
                    }
                    else
                    {
                        const size_t grain = std::max<size_t>(8, wideCount / 16);
                        tbb::parallel_for(
                            tbb::blocked_range<size_t>(0, wideCount, grain),
                            [=](const auto &range) {
                                for (size_t w = range.begin(); w != range.end(); ++w)
                                {
                                    applyRestitutionWide8AVX2(wideData[w], bodyData, params);
                                }
                            },
                            tbb::static_partitioner(), context);
                    }
                    for (SolverConstraint &sc : colorTailConstraints[c])
                    {
                        runTailRestitution(sc);
                    }
                }
                for (SolverConstraint &sc : overflowConstraints)
                {
                    runTailRestitution(sc);
                }
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
        for (const auto &[regIdx, rb] : solverTouchedWritableBodies)
        {
            const uint32_t localIdx = solverBodyLocalIndices[regIdx];
            const WideBodyState &state = solverWideBodyStates[localIdx];
            rb->velocity.x() = state.vx;
            rb->velocity.y() = state.vy;
            rb->angularVelocity = state.w;
            if (params.commitPositions)
            {
                solverBodyDeltas[regIdx] = SolverBodyDelta{state.dpX, state.dpY, state.dAngle};
                solverBodyIntegrated[regIdx] = 1;
            }
            solverBodyLocalIndices[regIdx] = UNMAPPED_SOLVER_BODY;
        }
        return true;
    }
#endif

    // General path (moving kinematic endpoints or non-AVX2 CPU).
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
            solverBodyStates.push_back(SolverBodyState{v, w, Eigen::Vector2f::Zero(), 0.0f, 0.0f, 0.0f});
            solverBodyForces.push_back(SolverBodyForce{0.0f, 0.0f, 0.0f, 0});
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
            prepareSolverConstraint(solverPreStepJobs[i], params, solverConstraints[i]);
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
                    prepareSolverConstraint(solverPreStepJobs[i], params, solverConstraints[i]);
                    solverConstraints[i].bodyIndexA = static_cast<uint32_t>(solverEndpoints[i].first);
                    solverConstraints[i].bodyIndexB = static_cast<uint32_t>(solverEndpoints[i].second);
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

    for (size_t substep = 0; substep < params.subSteps; ++substep)
    {
        if (params.integrateForces && h > 0.0f)
        {
            for (size_t i = 1; i < bodyCount; ++i)
            {
                if (solverBodyForces[i].isDynamic)
                {
                    solverBodyStates[i].velocity.x() += solverBodyForces[i].accelX * h;
                    solverBodyStates[i].velocity.y() += solverBodyForces[i].accelY * h;
                    solverBodyStates[i].angularVelocity += solverBodyForces[i].angAccel * h;
                }
            }
        }
        if (substep == 0)
        {
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
        }

        const auto runScalarColorPass = [&](bool useBias) {
            for (uint8_t c = 0; c < IslandColorGraph::MAX_PARALLEL_COLORS; ++c)
            {
                const size_t begin = colorBegin[c];
                const size_t end = colorEnd[c];
                if (!allowParallel || end - begin < parallelSolverBatchSize)
                {
                    for (size_t i = begin; i < end; ++i)
                    {
                        SolverConstraint &sc = solverConstraints[i];
                        solveConstraintSoft(sc, solverBodyStates[sc.bodyIndexA], solverBodyStates[sc.bodyIndexB],
                                            params, useBias);
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
                                solveConstraintSoft(sc, solverBodyStates[sc.bodyIndexA],
                                                    solverBodyStates[sc.bodyIndexB], params, useBias);
                            }
                        },
                        tbb::static_partitioner(), context);
                }
            }
            for (size_t i = overflowBegin; i < overflowEnd; ++i)
            {
                SolverConstraint &sc = solverConstraints[i];
                solveConstraintSoft(sc, solverBodyStates[sc.bodyIndexA], solverBodyStates[sc.bodyIndexB], params,
                                    useBias);
            }
        };

        runScalarColorPass(true);
        if (h > 0.0f)
        {
            for (size_t i = 1; i < bodyCount; ++i)
            {
                solverBodyStates[i].deltaPosition += solverBodyStates[i].velocity * h;
                solverBodyStates[i].deltaAngle += solverBodyStates[i].angularVelocity * h;
            }
        }
        runScalarColorPass(false);

        if (substep + 1 == params.subSteps && anyRestitution)
        {
            for (uint8_t c = 0; c < IslandColorGraph::MAX_PARALLEL_COLORS; ++c)
            {
                const size_t begin = colorBegin[c];
                const size_t end = colorEnd[c];
                for (size_t i = begin; i < end; ++i)
                {
                    SolverConstraint &sc = solverConstraints[i];
                    applyRestitutionSoft(sc, solverBodyStates[sc.bodyIndexA], solverBodyStates[sc.bodyIndexB], params);
                }
            }
            for (size_t i = overflowBegin; i < overflowEnd; ++i)
            {
                SolverConstraint &sc = solverConstraints[i];
                applyRestitutionSoft(sc, solverBodyStates[sc.bodyIndexA], solverBodyStates[sc.bodyIndexB], params);
            }
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
        if (params.commitPositions)
        {
            solverBodyDeltas[regIdx] =
                SolverBodyDelta{state.deltaPosition.x(), state.deltaPosition.y(), state.deltaAngle};
            solverBodyIntegrated[regIdx] = 1;
        }
        solverBodyLocalIndices[regIdx] = UNMAPPED_SOLVER_BODY;
    }
    return true;
}

// ============================================================================
// Top-Level Dispatch
// ============================================================================

void PhysicsManager::solveConstraintIterations(const SoftStepParams &params)
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
                solveColoredIsland(island, params, allowParallel))
            {
                anyColored = true;
                continue;
            }
            solvePersistentIsland(island, params, serialSolverScratch);
        }
        if (anyColored && allowParallel)
        {
            PLATFORMATOR_WORK_ADD(solverOrderedBatchDispatches, 1);
        }
        return;
    }

    if (allowParallel)
    {
        solveIslands(params, totalContacts);
        return;
    }

    solveConstraintsSerial(params);
}

void PhysicsManager::solveConstraintsSerial(const SoftStepParams &params)
{
    buildIslandWorkUnits();
    executeIslandWorkUnits(0, islandWorkUnits.size(), params, serialSolverScratch);
}

void PhysicsManager::solveIslands(const SoftStepParams &params, size_t totalContacts)
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
            executeIslandWorkUnits(begin, end, params, scratch);
        },
        context);
}

// ============================================================================
// Constraint Pre-Step & Scalar Soft Step Kernels
// ============================================================================

void PhysicsManager::prepareSolverConstraint(const SolverPreStepJob &job, const SoftStepParams &params,
                                             SolverConstraint &outConstraint) const
{
    Collision *collision = job.collision;
    Rigidbody *rbA = job.rbA;
    Rigidbody *rbB = job.rbB;

    const bool dynA = rbA->getBodyType() == DYNAMIC && !rbA->getIsSleeping();
    const bool dynB = rbB->getBodyType() == DYNAMIC && !rbB->getIsSleeping();
    const float invMassA = dynA ? rbA->getInverseMass() : 0.0f;
    const float invMassB = dynB ? rbB->getInverseMass() : 0.0f;
    const float invInertiaA = dynA ? rbA->getInverseMomentOfInertia() : 0.0f;
    const float invInertiaB = dynB ? rbB->getInverseMomentOfInertia() : 0.0f;

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
    outConstraint.solverRestitution = combinedRestitution;
    outConstraint.pointCount = pointCount;

    Eigen::Vector2f velA = Eigen::Vector2f::Zero();
    Eigen::Vector2f velB = Eigen::Vector2f::Zero();
    float angVelA = 0.0f;
    float angVelB = 0.0f;
    if (combinedRestitution > 0.0f)
    {
        velA = rbA->getVelocity();
        velB = rbB->getVelocity();
        angVelA = rbA->getAngularVelocity();
        angVelB = rbB->getAngularVelocity();
        if (params.integrateForces && params.h > 0.0f)
        {
            if (dynA)
            {
                const SolverBodyForce fA = makeBodyForce(rbA);
                velA.x() += fA.accelX * params.h;
                velA.y() += fA.accelY * params.h;
                angVelA += fA.angAccel * params.h;
            }
            if (dynB)
            {
                const SolverBodyForce fB = makeBodyForce(rbB);
                velB.x() += fB.accelX * params.h;
                velB.y() += fB.accelY * params.h;
                angVelB += fB.angAccel * params.h;
            }
        }
    }

    for (uint8_t i = 0; i < pointCount; ++i)
    {
        const Eigen::Vector2f rA = contactPoints.points[i].point.point - positionA;
        const Eigen::Vector2f rB = contactPoints.points[i].point.point - positionB;
        outConstraint.rA[i] = rA;
        outConstraint.rB[i] = rB;

        const float rA2 = rA.dot(rA);
        const float rB2 = rB.dot(rB);
        const float rnA = rA.dot(normal);
        const float rnB = rB.dot(normal);
        const float kNormal = inverseMassSum + invInertiaA * (rA2 - rnA * rnA) + invInertiaB * (rB2 - rnB * rnB);
        outConstraint.massNormal[i] = kNormal > 0.0f ? 1.0f / kNormal : 0.0f;

        const float rtA = rA.dot(tangent);
        const float rtB = rB.dot(tangent);
        const float kTangent = inverseMassSum + invInertiaA * (rA2 - rtA * rtA) + invInertiaB * (rB2 - rtB * rtB);
        outConstraint.massTangent[i] = kTangent > 0.0f ? 1.0f / kTangent : 0.0f;

        outConstraint.baseSeparation[i] = contactPoints.points[i].separation;
        outConstraint.relativeVelocity[i] =
            combinedRestitution > 0.0f
                ? ((velB + crossSV(angVelB, rB)) - (velA + crossSV(angVelA, rA))).dot(normal)
                : 0.0f;
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

void PhysicsManager::solveConstraintSoft(SolverConstraint &constraint, SolverBodyState &bodyA, SolverBodyState &bodyB,
                                         const SoftStepParams &params, bool useBias) noexcept
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
    const Eigen::Vector2f dp = bodyB.deltaPosition - bodyA.deltaPosition;

    for (uint8_t i = 0; i < constraint.pointCount; ++i)
    {
        const Eigen::Vector2f &rA = constraint.rA[i];
        const Eigen::Vector2f &rB = constraint.rB[i];

        Eigen::Vector2f relativeVelocity = (vB + crossSV(wB, rB)) - (vA + crossSV(wA, rA));
        const float vn = relativeVelocity.dot(normal);

        float normalImpulse = 0.0f;
        if (useBias)
        {
            const Eigen::Vector2f ds = dp + crossSV(bodyB.deltaAngle, rB) - crossSV(bodyA.deltaAngle, rA);
            const float sep = constraint.baseSeparation[i] + ds.dot(normal);
            float bias = 0.0f;
            if (sep > 0.0f)
            {
                bias = sep * params.invH;
            }
            else
            {
                const float penDepth = std::min(0.0f, sep + params.slop);
                bias = std::max(params.biasRate * penDepth, params.negMaxPushSpeed);
            }
            const float rhs =
                params.massScale * (vn + bias) + params.impulseScale * constraint.accumulatedNormalImpulse[i];
            normalImpulse = -constraint.massNormal[i] * rhs;
        }
        else
        {
            normalImpulse = -constraint.massNormal[i] * vn;
        }

        const float oldNormal = constraint.accumulatedNormalImpulse[i];
        const float newNormal = std::max(oldNormal + normalImpulse, 0.0f);
        constraint.accumulatedNormalImpulse[i] = newNormal;
        const float appliedNormal = newNormal - oldNormal;

        const Eigen::Vector2f impulse = appliedNormal * normal;
        applyLocalImpulse(vA, wA, invMassA, invInertiaA, -impulse, rA);
        applyLocalImpulse(vB, wB, invMassB, invInertiaB, impulse, rB);

        relativeVelocity = (vB + crossSV(wB, rB)) - (vA + crossSV(wA, rA));
        const float vt = relativeVelocity.dot(tangent);
        float tangentImpulse = constraint.massTangent[i] * (-vt);

        const float oldTangent = constraint.accumulatedTangentImpulse[i];
        const float maxTangent = constraint.solverFriction * newNormal;
        const float newTangent = Clamp(oldTangent + tangentImpulse, -maxTangent, maxTangent);
        constraint.accumulatedTangentImpulse[i] = newTangent;
        tangentImpulse = newTangent - oldTangent;

        const Eigen::Vector2f contactImpulse = tangentImpulse * tangent;
        applyLocalImpulse(vA, wA, invMassA, invInertiaA, -contactImpulse, rA);
        applyLocalImpulse(vB, wB, invMassB, invInertiaB, contactImpulse, rB);
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

void PhysicsManager::applyRestitutionSoft(SolverConstraint &constraint, SolverBodyState &bodyA, SolverBodyState &bodyB,
                                          const SoftStepParams &params) noexcept
{
    const float restitution = constraint.solverRestitution;
    if (restitution <= 0.0f)
    {
        return;
    }

    const float invMassA = bodyA.inverseMass;
    const float invInertiaA = bodyA.inverseMomentOfInertia;
    const float invMassB = bodyB.inverseMass;
    const float invInertiaB = bodyB.inverseMomentOfInertia;

    Eigen::Vector2f vA = bodyA.velocity;
    float wA = bodyA.angularVelocity;
    Eigen::Vector2f vB = bodyB.velocity;
    float wB = bodyB.angularVelocity;
    const Eigen::Vector2f &normal = constraint.normal;
    bool changed = false;

    for (uint8_t i = 0; i < constraint.pointCount; ++i)
    {
        const float relVel = constraint.relativeVelocity[i];
        if (relVel >= -params.restitutionThreshold)
        {
            continue;
        }
        const Eigen::Vector2f &rA = constraint.rA[i];
        const Eigen::Vector2f &rB = constraint.rB[i];
        const Eigen::Vector2f currentRelVel = (vB + crossSV(wB, rB)) - (vA + crossSV(wA, rA));
        const float vn = currentRelVel.dot(normal);
        const float applied = std::max(constraint.massNormal[i] * (-restitution * relVel - vn), 0.0f);
        if (applied <= 0.0f)
        {
            continue;
        }
        const Eigen::Vector2f impulse = applied * normal;
        applyLocalImpulse(vA, wA, invMassA, invInertiaA, -impulse, rA);
        applyLocalImpulse(vB, wB, invMassB, invInertiaB, impulse, rB);
        changed = true;
    }

    if (!changed)
    {
        return;
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
