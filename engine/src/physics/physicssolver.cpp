#include "physics/physicsmanager.h"
#include "physics/physicsgeometry.h"

#include "benchmark/benchmark.h"
#include "benchmark/workprobe.h"
#include "core/constants.h"
#include "core/gamemanager.h"
#include "core/runtimeaccess.h"

#include <oneapi/tbb/global_control.h>
#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/task_arena.h>
#include <oneapi/tbb/task_group.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace
{
inline void applyLocalImpulse(Eigen::Vector2f &velocity, float &angularVelocity, float inverseMass,
                              float inverseMomentOfInertia, const Eigen::Vector2f &impulse,
                              const Eigen::Vector2f &arm) noexcept
{
    velocity += impulse * inverseMass;
    angularVelocity += inverseMomentOfInertia * (arm.x() * impulse.y() - arm.y() * impulse.x());
}

inline Eigen::Vector2f relativeContactVelocity(const Eigen::Vector2f &vA, float wA, const Eigen::Vector2f &rA,
                                               const Eigen::Vector2f &vB, float wB, const Eigen::Vector2f &rB) noexcept
{
    return (vB + physics_geometry::crossSV(wB, rB)) - (vA + physics_geometry::crossSV(wA, rA));
}
} // namespace

// ============================================================================
// Soft Step Parameters, Entry Points & Body Integration Helpers
// ============================================================================

PhysicsManager::SoftStepParams PhysicsManager::makeSoftStepParams(double timeDelta) const noexcept
{
    SoftStepParams params;
    params.subSteps = COLLISION_SOLVER_SUBSTEPS;
    const float dt = timeDelta > 0.0 ? static_cast<float>(timeDelta) : 0.0f;
    params.h = dt > 0.0f ? dt / static_cast<float>(params.subSteps) : 0.0f;
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
    return params;
}

PhysicsManager::SolverBodyForce PhysicsManager::makeBodyForce(const Rigidbody *rb) const noexcept
{
    if (rb == nullptr || !rb->isAwakeDynamic())
    {
        return SolverBodyForce{};
    }
    const Eigen::Vector2f accel =
        rb->getForce() * rb->getInverseMass() + (rb->getGravity() ? gravityVector : Eigen::Vector2f::Zero());
    const float angAccel = rb->getTorque() * rb->getInverseMomentOfInertia();
    return SolverBodyForce{accel.x(), accel.y(), angAccel, 1};
}

void PhysicsManager::commitWritableBodies(const std::vector<WritableSolverBody> &writable,
                                          const std::vector<SolverBodyState> &states) noexcept
{
    for (const WritableSolverBody &entry : writable)
    {
        const SolverBodyState &state = states[entry.localIndex];
        entry.body->velocity = Eigen::Vector2f(state.vx, state.vy);
        entry.body->angularVelocity = state.w;
        const size_t awakeIndex = entry.body->awakeDynamicIndex;
        if (awakeIndex != SIZE_MAX)
        {
            solverBodyDeltas[awakeIndex] = SolverBodyDelta{state.dpX, state.dpY, state.dAngle, solverStep};
        }
    }
}

void PhysicsManager::integrateSolverVelocities(SolverBodyState *states, const SolverBodyForce *forces,
                                               size_t begin, size_t end, float h) noexcept
{
    for (size_t i = begin; i < end; ++i)
    {
        if (forces[i].isDynamic)
        {
            states[i].vx += forces[i].accelX * h;
            states[i].vy += forces[i].accelY * h;
            states[i].w += forces[i].angAccel * h;
        }
    }
}

void PhysicsManager::integrateSolverPositions(SolverBodyState *states, size_t begin, size_t end, float h) noexcept
{
    for (size_t i = begin; i < end; ++i)
    {
        states[i].dpX += states[i].vx * h;
        states[i].dpY += states[i].vy * h;
        states[i].dAngle += states[i].w * h;
    }
}

void PhysicsManager::resolveKinematicPairs()
{
    for (ColliderPair *pair : kinematicSolverPairs)
    {
        Collision *collision = nullptr;
        Rigidbody *rbA = nullptr;
        Rigidbody *rbB = nullptr;
        if (extractSolverPair(pair, collision, rbA, rbB))
        {
            resolveKinematicCollision(collision, rbA, rbB);
        }
    }
}

void PhysicsManager::stepDynamics(double timeDelta)
{
    solveDynamics(timeDelta);
    commitMovedBodies(timeDelta, /*useSolverDeltas=*/true);
    reconcileChangedBodies();
    updateSleepingStates(timeDelta);

#if PLATFORMATOR_ENABLE_BENCHMARKS
    PLATFORMATOR_BENCH_SET_COUNTER(AwakeDynamicBodyCount, countAwakeDynamicBodies());
#endif
}

void PhysicsManager::solveDynamics(double timeDelta)
{
    PLATFORMATOR_BENCH_SCOPE(ResolveCollisions);
    resolveKinematicPairs();
    splitDirtyIslands();
    wakePersistentIslandEndpoints();
    reconcileChangedBodies();

    PLATFORMATOR_WORK_ADD(forceBodies, awakeDynamicBodies.size());
    // Only deltas written during this solve may be committed by the movement pass.
    // Contact loss and reused registration slots both fall back to fresh integration.
    if (++solverStep == 0)
    {
        for (auto &delta : solverBodyDeltas)
            delta.step = 0;
        ++solverStep;
    }
    if (!activeIslandIds.empty())
    {
        if (solverBodyDeltas.size() < awakeDynamicBodies.size())
        {
            PLATFORMATOR_WORK_ADD(solverDeltaSlotsInitialized, awakeDynamicBodies.size() - solverBodyDeltas.size());
            solverBodyDeltas.resize(awakeDynamicBodies.size());
        }
        solveConstraintIterations(makeSoftStepParams(timeDelta));
    }
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
    auto &localIndices = scratch.bodyLocalIndices;
    auto &kinematics = scratch.kinematicBodies;
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
    kinematics.clear();
    localIndices.assign(island.bodies.size(), UNMAPPED_SOLVER_BODY);

    const auto mapBody = [&](Rigidbody *rb) -> uint32_t {
        if (!rb->isAwakeDynamic())
        {
            if (rb->isMovingKinematic())
            {
                for (const auto &[kinBody, kinSlot] : kinematics)
                {
                    if (kinBody == rb)
                    {
                        return kinSlot;
                    }
                }
                const uint32_t localIdx = static_cast<uint32_t>(bodies.size());
                const auto &v = rb->getVelocity();
                bodies.push_back(SolverBodyState{v.x(), v.y(), rb->getAngularVelocity(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f});
                bodyForces.push_back(SolverBodyForce{});
                kinematics.emplace_back(rb, localIdx);
                return localIdx;
            }
            return 0;
        }
        if (rb->solverIslandId == island.id && rb->solverIslandBodyIndex < localIndices.size())
        {
            uint32_t &mapped = localIndices[rb->solverIslandBodyIndex];
            if (mapped == UNMAPPED_SOLVER_BODY)
            {
                mapped = static_cast<uint32_t>(bodies.size());
                const auto &v = rb->getVelocity();
                bodies.push_back(SolverBodyState{v.x(), v.y(), rb->getAngularVelocity(), rb->getInverseMass(), 0.0f,
                                                 0.0f, 0.0f, rb->getInverseMomentOfInertia()});
                bodyForces.push_back(makeBodyForce(rb));
                writable.push_back(WritableSolverBody{rb->getPhysicsManagerIndex(), mapped, rb});
            }
            return mapped;
        }
        for (const WritableSolverBody &entry : writable)
        {
            if (entry.body == rb)
            {
                return entry.localIndex;
            }
        }
        const uint32_t localIdx = static_cast<uint32_t>(bodies.size());
        const auto &v = rb->getVelocity();
        bodies.push_back(SolverBodyState{v.x(), v.y(), rb->getAngularVelocity(), rb->getInverseMass(), 0.0f, 0.0f, 0.0f,
                                         rb->getInverseMomentOfInertia()});
        bodyForces.push_back(makeBodyForce(rb));
        writable.push_back(WritableSolverBody{SIZE_MAX, localIdx, rb});
        return localIdx;
    };

    bool anyRestitution = false;
    for (ColliderPair *pair : island.contacts)
    {
        Collision *collision = nullptr;
        Rigidbody *rbA = nullptr;
        Rigidbody *rbB = nullptr;
        if (!extractSolverPair(pair, collision, rbA, rbB))
        {
            continue;
        }
        local.emplace_back();
        SolverConstraint &c = local.back();
        prepareSolverConstraint(SolverPreStepJob{collision, rbA, rbB, mapBody(rbA), mapBody(rbB)}, params, c);
        anyRestitution |= constraintHasActiveRestitution(c, params.restitutionThreshold);
    }

    if (local.empty())
    {
        return;
    }

    const size_t bodyCount = bodies.size();
    const float h = params.h;

    runSoftSteps(
        params, anyRestitution,
        [&] { integrateSolverVelocities(bodies.data(), bodyForces.data(), 1, bodyCount, h); },
        [&] {
            for (const SolverConstraint &constraint : local)
                applyWarmStart(constraint, bodies[constraint.bodyIndexA], bodies[constraint.bodyIndexB]);
        },
        [&](auto useBias) {
            for (SolverConstraint &constraint : local)
                solveConstraintSoft<decltype(useBias)::value>(constraint, bodies[constraint.bodyIndexA],
                                                              bodies[constraint.bodyIndexB], params);
        },
        [&] { integrateSolverPositions(bodies.data(), 1, bodyCount, h); },
        [&] {
            for (SolverConstraint &constraint : local)
                applyRestitutionSoft(constraint, bodies[constraint.bodyIndexA], bodies[constraint.bodyIndexB], params);
        });

    for (const SolverConstraint &constraint : local)
    {
        storeConstraintImpulses(constraint);
    }
    commitWritableBodies(writable, bodies);
}

// ============================================================================
// 8-Wide AVX2 Independent Island-Group Soft Step Solver (Zero-Barrier Substeps)
// ============================================================================

bool PhysicsManager::solvePersistentIslandGroup8(const int32_t *islandIds, size_t groupSize,
                                                 const SoftStepParams &params, IslandSolverScratch &scratch)
{
#if PLATFORMATOR_HAS_RUNTIME_AVX2
    using namespace platformator_solver_simd;
    auto &bodies = scratch.bodies;
    auto &bodyForces = scratch.bodyForces;
    auto &bodyPrep = scratch.bodyPrep;
    auto &writable = scratch.writableBodies;
    auto &localIndices = scratch.bodyLocalIndices;
    auto &kinematics = scratch.kinematicBodies;
    auto &wideSteps = scratch.wideSteps;
    auto &wideCold = scratch.wideCold;

    size_t maxPossibleSteps = 0;
    size_t laneBodyOffset[8] = {};
    size_t totalLaneBodies = 0;
    for (size_t lane = 0; lane < groupSize; ++lane)
    {
        const PersistentIsland &island = persistentIslands[static_cast<size_t>(islandIds[lane])];
        maxPossibleSteps = std::max(maxPossibleSteps, island.contacts.size());
        laneBodyOffset[lane] = totalLaneBodies;
        totalLaneBodies += island.bodies.size();
    }
    if (maxPossibleSteps == 0)
    {
        return true;
    }

    bodies.clear();
    bodyForces.clear();
    bodyPrep.clear();
    if (bodies.capacity() < totalLaneBodies + 1)
    {
        bodies.reserve(totalLaneBodies + 1);
        bodyForces.reserve(totalLaneBodies + 1);
        bodyPrep.reserve(totalLaneBodies + 1);
        writable.reserve(totalLaneBodies);
    }
    bodies.push_back(SolverBodyState{}); // slot 0: shared static/sleeping zero-velocity body
    bodyForces.push_back(SolverBodyForce{});
    bodyPrep.push_back(BodyPrepCache{});
    writable.clear();
    kinematics.clear();
    localIndices.assign(totalLaneBodies, UNMAPPED_SOLVER_BODY);

    // Pre-populate dynamic body slots once per island to eliminate per-contact GameObject pointer chasing.
    for (size_t lane = 0; lane < groupSize; ++lane)
    {
        const PersistentIsland &island = persistentIslands[static_cast<size_t>(islandIds[lane])];
        const size_t bodyBase = laneBodyOffset[lane];
        for (size_t bi = 0; bi < island.bodies.size(); ++bi)
        {
            Rigidbody *rb = island.bodies[bi];
            if (!rb->isAwakeDynamic())
            {
                continue;
            }
            const uint32_t mapped = static_cast<uint32_t>(bodies.size());
            localIndices[bodyBase + bi] = mapped;
            const auto &v = rb->getVelocity();
            bodies.push_back(SolverBodyState{v.x(), v.y(), rb->getAngularVelocity(), rb->getInverseMass(), 0.0f, 0.0f,
                                             0.0f, rb->getInverseMomentOfInertia()});
            bodyForces.push_back(makeBodyForce(rb));
            const auto &pos = rb->getGameObject()->worldPosition;
            bodyPrep.push_back(BodyPrepCache{pos.x(), pos.y(), rb->getFriction(), rb->getRestitution()});
            writable.push_back(WritableSolverBody{rb->getPhysicsManagerIndex(), mapped, rb});
        }
    }

    bool allIndexed = true;
    const auto mapBody = [&](const PersistentIsland &island, size_t bodyBase, Rigidbody *rb) -> uint32_t {
        if (!rb->isAwakeDynamic())
        {
            if (rb->isMovingKinematic())
            {
                for (const auto &[kinBody, kinSlot] : kinematics)
                {
                    if (kinBody == rb)
                    {
                        return kinSlot;
                    }
                }
                const uint32_t localIdx = static_cast<uint32_t>(bodies.size());
                const auto &v = rb->getVelocity();
                bodies.push_back(SolverBodyState{v.x(), v.y(), rb->getAngularVelocity(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f});
                bodyForces.push_back(SolverBodyForce{});
                const auto &pos = rb->getGameObject()->worldPosition;
                bodyPrep.push_back(BodyPrepCache{pos.x(), pos.y(), rb->getFriction(), rb->getRestitution()});
                kinematics.emplace_back(rb, localIdx);
                return localIdx;
            }
            return 0;
        }
        if (rb->solverIslandId != island.id || rb->solverIslandBodyIndex >= island.bodies.size())
        {
            allIndexed = false;
            return 0;
        }
        const uint32_t mapped = localIndices[bodyBase + rb->solverIslandBodyIndex];
        if (mapped == UNMAPPED_SOLVER_BODY)
        {
            allIndexed = false;
            return 0;
        }
        return mapped;
    };

    wideSteps.resize(maxPossibleSteps);
    wideCold.resize(maxPossibleSteps);

    const PersistentIsland *laneIslands[8] = {};
    ColliderPair *const *laneContacts[8] = {};
    size_t laneContactSizes[8] = {};
    size_t laneCursors[8] = {};
    for (size_t lane = 0; lane < groupSize; ++lane)
    {
        const PersistentIsland &island = persistentIslands[static_cast<size_t>(islandIds[lane])];
        laneIslands[lane] = &island;
        laneContacts[lane] = island.contacts.data();
        laneContactSizes[lane] = island.contacts.size();
    }

    size_t stepCount = 0;
    bool anyRestitution = false;
    const float h = params.h;

    // Step-major packing keeps WideConstraint8 (832 B) hot in L1D across all 8 lanes and fuses AVX2 mass finalization.
    while (true)
    {
        WideConstraint8 &wide = wideSteps[stepCount];
        WideConstraint8Cold &cold = wideCold[stepCount];
        wide.resetMasks();
        bool anyLaneActive = false;

        for (size_t lane = 0; lane < 8; ++lane)
        {
            if (lane >= groupSize)
            {
                clearWideLane(wide, cold, lane);
                continue;
            }
            const PersistentIsland &island = *laneIslands[lane];
            ColliderPair *const *contacts = laneContacts[lane];
            const size_t totalContacts = laneContactSizes[lane];
            size_t &cursor = laneCursors[lane];

            Collision *collision = nullptr;
            Rigidbody *rbA = nullptr;
            Rigidbody *rbB = nullptr;
            ClipPointsWithData *contactPointsPtr = nullptr;
            uint8_t pointCount = 0;

            while (cursor < totalContacts)
            {
                if (cursor + 4 < totalContacts)
                {
                    __builtin_prefetch(contacts[cursor + 4], 0, 1);
                }
                ColliderPair *pair = contacts[cursor++];
                if (!extractSolverPair(pair, collision, rbA, rbB))
                {
                    continue;
                }
                ClipPointsWithData &cp = collision->getContactPoints();
                const uint8_t pc = static_cast<uint8_t>(std::min<size_t>(cp.count, 2));
                if (pc == 0)
                {
                    continue;
                }
                contactPointsPtr = &cp;
                pointCount = pc;
                break;
            }

            if (pointCount == 0)
            {
                clearWideLane(wide, cold, lane);
                continue;
            }

            anyLaneActive = true;
            const size_t bodyBase = laneBodyOffset[lane];
            const uint32_t idxA = mapBody(island, bodyBase, rbA);
            const uint32_t idxB = mapBody(island, bodyBase, rbB);
            if (!allIndexed)
            {
                return false;
            }

            const uint8_t laneBit = static_cast<uint8_t>(1u << lane);
            const SolverBodyState &bA = bodies[idxA];
            const SolverBodyState &bB = bodies[idxB];
            const float posAx = idxA != 0 ? bodyPrep[idxA].posX : 0.0f;
            const float posAy = idxA != 0 ? bodyPrep[idxA].posY : 0.0f;
            const float posBx = idxB != 0 ? bodyPrep[idxB].posX : 0.0f;
            const float posBy = idxB != 0 ? bodyPrep[idxB].posY : 0.0f;
            const float frictionA = idxA != 0 ? bodyPrep[idxA].friction : rbA->getFriction();
            const float frictionB = idxB != 0 ? bodyPrep[idxB].friction : rbB->getFriction();
            const float restitutionA = idxA != 0 ? bodyPrep[idxA].restitution : rbA->getRestitution();
            const float restitutionB = idxB != 0 ? bodyPrep[idxB].restitution : rbB->getRestitution();
            const float combinedFriction = (frictionA == frictionB) ? frictionA : std::sqrt(frictionA * frictionB);
            const float combinedRestitution = std::max(restitutionA, restitutionB);

            const Eigen::Vector2f &normal = collision->getNormal();
            const float nX = normal.x();
            const float nY = normal.y();

            wide.bodyIndexA[lane] = idxA;
            wide.bodyIndexB[lane] = idxB;
            wide.normalX[lane] = nX;
            wide.normalY[lane] = nY;
            wide.friction[lane] = combinedFriction;
            wide.invIA[lane] = bA.inverseMomentOfInertia;
            wide.invIB[lane] = bB.inverseMomentOfInertia;
            if (bA.inverseMass != 0.0f || bA.inverseMomentOfInertia != 0.0f)
            {
                wide.writeMaskA |= laneBit;
            }
            if (bB.inverseMass != 0.0f || bB.inverseMomentOfInertia != 0.0f)
            {
                wide.writeMaskB |= laneBit;
            }

            cold.contactPoints[lane] = contactPointsPtr;
            cold.pointCount[lane] = pointCount;
            cold.restitution[lane] = combinedRestitution;

            const ClipPointWithData &cp0 = contactPointsPtr->points[0];
            const float p0x = cp0.point.point.x();
            const float p0y = cp0.point.point.y();
            const float rAx0 = idxA != 0 ? (p0x - posAx) : 0.0f;
            const float rAy0 = idxA != 0 ? (p0y - posAy) : 0.0f;
            const float rBx0 = idxB != 0 ? (p0x - posBx) : 0.0f;
            const float rBy0 = idxB != 0 ? (p0y - posBy) : 0.0f;
            const float rnA0 = rAx0 * nY - rAy0 * nX;
            const float rnB0 = rBx0 * nY - rBy0 * nX;
            wide.rnA0[lane] = rnA0;
            wide.rnB0[lane] = rnB0;
            wide.rtA0[lane] = rAx0 * nX + rAy0 * nY;
            wide.rtB0[lane] = rBx0 * nX + rBy0 * nY;
            wide.baseSep0[lane] = cp0.separation;
            wide.accumNormal0[lane] = cp0.accumulatedNormalImpulse;
            wide.accumTangent0[lane] = cp0.accumulatedTangentImpulse;

            float rnA1 = 0.0f;
            float rnB1 = 0.0f;
            if (pointCount == 2)
            {
                const ClipPointWithData &cp1 = contactPointsPtr->points[1];
                const float p1x = cp1.point.point.x();
                const float p1y = cp1.point.point.y();
                const float rAx1 = idxA != 0 ? (p1x - posAx) : 0.0f;
                const float rAy1 = idxA != 0 ? (p1y - posAy) : 0.0f;
                const float rBx1 = idxB != 0 ? (p1x - posBx) : 0.0f;
                const float rBy1 = idxB != 0 ? (p1y - posBy) : 0.0f;
                rnA1 = rAx1 * nY - rAy1 * nX;
                rnB1 = rBx1 * nY - rBy1 * nX;
                wide.rnA1[lane] = rnA1;
                wide.rnB1[lane] = rnB1;
                wide.rtA1[lane] = rAx1 * nX + rAy1 * nY;
                wide.rtB1[lane] = rBx1 * nX + rBy1 * nY;
                wide.baseSep1[lane] = cp1.separation;
                wide.accumNormal1[lane] = cp1.accumulatedNormalImpulse;
                wide.accumTangent1[lane] = cp1.accumulatedTangentImpulse;
                wide.twoPointMask |= laneBit;
            }
            else
            {
                wide.rnA1[lane] = 0.0f;
                wide.rnB1[lane] = 0.0f;
                wide.rtA1[lane] = 0.0f;
                wide.rtB1[lane] = 0.0f;
                wide.massNormal1[lane] = 0.0f;
                wide.massTangent1[lane] = 0.0f;
                wide.baseSep1[lane] = 0.0f;
                wide.accumNormal1[lane] = 0.0f;
                wide.accumTangent1[lane] = 0.0f;
            }

            if (combinedRestitution > 0.0f)
            {
                float vAx = bA.vx;
                float vAy = bA.vy;
                float wA = bA.w;
                float vBx = bB.vx;
                float vBy = bB.vy;
                float wB = bB.w;
                if (h > 0.0f)
                {
                    if (bodyForces[idxA].isDynamic)
                    {
                        vAx += bodyForces[idxA].accelX * h;
                        vAy += bodyForces[idxA].accelY * h;
                        wA += bodyForces[idxA].angAccel * h;
                    }
                    if (bodyForces[idxB].isDynamic)
                    {
                        vBx += bodyForces[idxB].accelX * h;
                        vBy += bodyForces[idxB].accelY * h;
                        wB += bodyForces[idxB].angAccel * h;
                    }
                }
                const float dvN = (vBx - vAx) * nX + (vBy - vAy) * nY;
                const float rv0 = dvN + wB * rnB0 - wA * rnA0;
                cold.relVel0[lane] = rv0;
                const float rv1 = pointCount == 2 ? (dvN + wB * rnB1 - wA * rnA1) : 0.0f;
                cold.relVel1[lane] = rv1;
                if (rv0 < -params.restitutionThreshold || (pointCount == 2 && rv1 < -params.restitutionThreshold))
                {
                    wide.restitutionMask |= laneBit;
                    anyRestitution = true;
                }
            }
            else
            {
                cold.relVel0[lane] = 0.0f;
                cold.relVel1[lane] = 0.0f;
            }
        }

        if (!anyLaneActive)
        {
            break;
        }
        finalizeWideEffectiveMasses8AVX2(wide, bodies.data());
        ++stepCount;
        if (stepCount == maxPossibleSteps)
        {
            break;
        }
    }

    if (stepCount == 0)
    {
        return true;
    }

    wideSteps.resize(stepCount);
    wideCold.resize(stepCount);
    SolverBodyState *bodyData = bodies.data();
    const size_t bodyCount = bodies.size();

    runSoftSteps(
        params, anyRestitution,
        [&] { integrateSolverVelocities(bodyData, bodyForces.data(), 1, bodyCount, h); },
        [&] {
            for (auto &wide : wideSteps)
                applyWarmStartWide8AVX2(wide, bodyData);
        },
        [&](auto useBias) {
            for (auto &wide : wideSteps)
                solveWide8SoftAVX2<decltype(useBias)::value>(wide, bodyData, params);
        },
        [&] { integrateSolverPositions(bodyData, 1, bodyCount, h); },
        [&] {
            for (size_t step = 0; step < stepCount; ++step)
                applyRestitutionWide8AVX2(wideSteps[step], wideCold[step], bodyData, params);
        });

    for (size_t step = 0; step < stepCount; ++step)
    {
        storeWideImpulses(wideSteps[step], wideCold[step]);
    }

    commitWritableBodies(writable, bodies);
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
    const bool allowParallel = totalContacts >= parallelConstraintThreshold && hasParallelPhysicsWorkers();

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

    const bool dynA = rbA->isAwakeDynamic();
    const bool dynB = rbB->isAwakeDynamic();
    const float invMassA = dynA ? rbA->getInverseMass() : 0.0f;
    const float invMassB = dynB ? rbB->getInverseMass() : 0.0f;
    const float invInertiaA = dynA ? rbA->getInverseMomentOfInertia() : 0.0f;
    const float invInertiaB = dynB ? rbB->getInverseMomentOfInertia() : 0.0f;

    const Eigen::Vector2f &positionA = rbA->getGameObject()->worldPosition;
    const Eigen::Vector2f &positionB = rbB->getGameObject()->worldPosition;

    const Eigen::Vector2f &normal = collision->getNormal();
    const Eigen::Vector2f tangent = physics_geometry::crossSV(1.0f, normal);
    const float inverseMassSum = invMassA + invMassB;
    ClipPointsWithData &contactPoints = collision->getContactPoints();
    const uint8_t pointCount = static_cast<uint8_t>(std::min<size_t>(contactPoints.count, 2));

    const float frictionA = rbA->getFriction();
    const float frictionB = rbB->getFriction();
    const float combinedFriction = (frictionA == frictionB) ? frictionA : std::sqrt(frictionA * frictionB);
    const float combinedRestitution = std::max(rbA->getRestitution(), rbB->getRestitution());

    outConstraint.contactPoints = &contactPoints;
    outConstraint.bodyIndexA = job.bodyIndexA;
    outConstraint.bodyIndexB = job.bodyIndexB;
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
        if (params.h > 0.0f)
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
            combinedRestitution > 0.0f ? relativeContactVelocity(velA, angVelA, rA, velB, angVelB, rB).dot(normal)
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

    Eigen::Vector2f vA(bodyA.vx, bodyA.vy);
    float wA = bodyA.w;
    Eigen::Vector2f vB(bodyB.vx, bodyB.vy);
    float wB = bodyB.w;

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
        bodyA.vx = vA.x();
        bodyA.vy = vA.y();
        bodyA.w = wA;
    }
    if (invMassB != 0.0f || invInertiaB != 0.0f)
    {
        bodyB.vx = vB.x();
        bodyB.vy = vB.y();
        bodyB.w = wB;
    }
}

template <bool UseBias>
void PhysicsManager::solveConstraintSoft(SolverConstraint &constraint, SolverBodyState &bodyA, SolverBodyState &bodyB,
                                         const SoftStepParams &params) noexcept
{
    const float invMassA = bodyA.inverseMass;
    const float invInertiaA = bodyA.inverseMomentOfInertia;
    const float invMassB = bodyB.inverseMass;
    const float invInertiaB = bodyB.inverseMomentOfInertia;

    Eigen::Vector2f vA(bodyA.vx, bodyA.vy);
    float wA = bodyA.w;
    Eigen::Vector2f vB(bodyB.vx, bodyB.vy);
    float wB = bodyB.w;

    const Eigen::Vector2f &normal = constraint.normal;
    const Eigen::Vector2f &tangent = constraint.tangent;
    Eigen::Vector2f dp = Eigen::Vector2f::Zero();
    if constexpr (UseBias)
    {
        dp = Eigen::Vector2f(bodyB.dpX - bodyA.dpX, bodyB.dpY - bodyA.dpY);
    }

    for (uint8_t i = 0; i < constraint.pointCount; ++i)
    {
        const Eigen::Vector2f &rA = constraint.rA[i];
        const Eigen::Vector2f &rB = constraint.rB[i];

        Eigen::Vector2f relativeVelocity = relativeContactVelocity(vA, wA, rA, vB, wB, rB);
        const float vn = relativeVelocity.dot(normal);

        float normalImpulse = 0.0f;
        if constexpr (UseBias)
        {
            const Eigen::Vector2f ds =
                dp + physics_geometry::crossSV(bodyB.dAngle, rB) - physics_geometry::crossSV(bodyA.dAngle, rA);
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
            (void)params;
            normalImpulse = -constraint.massNormal[i] * vn;
        }

        const float oldNormal = constraint.accumulatedNormalImpulse[i];
        const float newNormal = std::max(oldNormal + normalImpulse, 0.0f);
        constraint.accumulatedNormalImpulse[i] = newNormal;
        const float appliedNormal = newNormal - oldNormal;

        const Eigen::Vector2f impulse = appliedNormal * normal;
        applyLocalImpulse(vA, wA, invMassA, invInertiaA, -impulse, rA);
        applyLocalImpulse(vB, wB, invMassB, invInertiaB, impulse, rB);

        relativeVelocity = relativeContactVelocity(vA, wA, rA, vB, wB, rB);
        const float vt = relativeVelocity.dot(tangent);
        float tangentImpulse = constraint.massTangent[i] * (-vt);

        const float oldTangent = constraint.accumulatedTangentImpulse[i];
        const float maxTangent = constraint.solverFriction * newNormal;
        const float newTangent = std::clamp(oldTangent + tangentImpulse, -maxTangent, maxTangent);
        constraint.accumulatedTangentImpulse[i] = newTangent;
        tangentImpulse = newTangent - oldTangent;

        const Eigen::Vector2f contactImpulse = tangentImpulse * tangent;
        applyLocalImpulse(vA, wA, invMassA, invInertiaA, -contactImpulse, rA);
        applyLocalImpulse(vB, wB, invMassB, invInertiaB, contactImpulse, rB);
    }

    if (invMassA != 0.0f || invInertiaA != 0.0f)
    {
        bodyA.vx = vA.x();
        bodyA.vy = vA.y();
        bodyA.w = wA;
    }
    if (invMassB != 0.0f || invInertiaB != 0.0f)
    {
        bodyB.vx = vB.x();
        bodyB.vy = vB.y();
        bodyB.w = wB;
    }
}

template void PhysicsManager::solveConstraintSoft<true>(SolverConstraint &, SolverBodyState &, SolverBodyState &,
                                                        const SoftStepParams &) noexcept;
template void PhysicsManager::solveConstraintSoft<false>(SolverConstraint &, SolverBodyState &, SolverBodyState &,
                                                         const SoftStepParams &) noexcept;

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

    Eigen::Vector2f vA(bodyA.vx, bodyA.vy);
    float wA = bodyA.w;
    Eigen::Vector2f vB(bodyB.vx, bodyB.vy);
    float wB = bodyB.w;
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
        const float vn = relativeContactVelocity(vA, wA, rA, vB, wB, rB).dot(normal);
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
        bodyA.vx = vA.x();
        bodyA.vy = vA.y();
        bodyA.w = wA;
    }
    if (invMassB != 0.0f || invInertiaB != 0.0f)
    {
        bodyB.vx = vB.x();
        bodyB.vy = vB.y();
        bodyB.w = wB;
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
