#include "physics/physicsmanager.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

// ============================================================================
// Scalar Single-Island Soft-Step Solver
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
            return rb->isMovingKinematic() ? findOrAddKinematicBody(rb, bodies, bodyForces, kinematics) : 0u;
        }
        if (rb->solverIslandId == island.id && rb->solverIslandBodyIndex < localIndices.size())
        {
            uint32_t &mapped = localIndices[rb->solverIslandBodyIndex];
            if (mapped == UNMAPPED_SOLVER_BODY)
            {
                mapped = static_cast<uint32_t>(bodies.size());
                bodies.push_back(makeDynamicBodyState(rb));
                bodyForces.push_back(makeBodyForce(rb));
                writable.push_back(WritableSolverBody{mapped, rb});
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
        bodies.push_back(makeDynamicBodyState(rb));
        bodyForces.push_back(makeBodyForce(rb));
        writable.push_back(WritableSolverBody{localIdx, rb});
        return localIdx;
    };

    bool anyRestitution = false;
    for (ColliderPair *pair : island.contacts)
    {
        SolverContactPair contact;
        if (!extractSolverContactPair(pair, contact))
        {
            continue;
        }
        local.emplace_back();
        SolverConstraint &c = local.back();
        prepareSolverConstraint(
            SolverPreStepJob{contact.collision, contact.refBody, contact.incBody, mapBody(contact.refBody),
                             mapBody(contact.incBody)},
            params, c);
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
            {
                applyWarmStart(constraint, bodies[constraint.bodyIndexA], bodies[constraint.bodyIndexB]);
            }
        },
        [&](auto useBias) {
            for (SolverConstraint &constraint : local)
            {
                solveConstraintSoft<decltype(useBias)::value>(constraint, bodies[constraint.bodyIndexA],
                                                              bodies[constraint.bodyIndexB], params);
            }
        },
        [&] { integrateSolverPositions(bodies.data(), 1, bodyCount, h); },
        [&] {
            for (const SolverConstraint &constraint : local)
            {
                applyRestitutionSoft(constraint, bodies[constraint.bodyIndexA], bodies[constraint.bodyIndexB], params);
            }
        });

    for (const SolverConstraint &constraint : local)
    {
        storeConstraintImpulses(constraint);
    }
    commitWritableBodies(writable, bodies);
}

// ============================================================================
// Constraint Pre-Step & Scalar Soft-Step Kernels
// ============================================================================

void PhysicsManager::prepareSolverConstraint(const SolverPreStepJob &job, const SoftStepParams &params,
                                             SolverConstraint &outConstraint) const
{
    Collision *collision = job.collision;
    Rigidbody *rbA = job.rbA;
    Rigidbody *rbB = job.rbB;

    const bool dynA = rbA->isAwakeDynamic();
    const bool dynB = rbB->isAwakeDynamic();
    const bool activeA = dynA || rbA->isMovingKinematic();
    const bool activeB = dynB || rbB->isMovingKinematic();
    const float invMassA = dynA ? rbA->getInverseMass() : 0.0f;
    const float invMassB = dynB ? rbB->getInverseMass() : 0.0f;
    const float invInertiaA = dynA ? rbA->getInverseMomentOfInertia() : 0.0f;
    const float invInertiaB = dynB ? rbB->getInverseMomentOfInertia() : 0.0f;

    const Eigen::Vector2f positionA = activeA ? rbA->getGameObject()->worldPosition : Eigen::Vector2f::Zero();
    const Eigen::Vector2f positionB = activeB ? rbB->getGameObject()->worldPosition : Eigen::Vector2f::Zero();

    const Eigen::Vector2f &normal = collision->getNormal();
    const float nX = normal.x();
    const float nY = normal.y();
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
    outConstraint.solverFriction = combinedFriction;
    outConstraint.solverRestitution = combinedRestitution;
    outConstraint.pointCount = pointCount;

    float dvN = 0.0f;
    float angVelA = 0.0f;
    float angVelB = 0.0f;
    if (combinedRestitution > 0.0f)
    {
        Eigen::Vector2f velA = rbA->getVelocity();
        Eigen::Vector2f velB = rbB->getVelocity();
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
        dvN = (velB.x() - velA.x()) * nX + (velB.y() - velA.y()) * nY;
    }

    for (uint8_t i = 0; i < pointCount; ++i)
    {
        const ClipPointWithData &cp = contactPoints.points[i];
        const float px = cp.point.point.x();
        const float py = cp.point.point.y();
        const float rAx = activeA ? (px - positionA.x()) : 0.0f;
        const float rAy = activeA ? (py - positionA.y()) : 0.0f;
        const float rBx = activeB ? (px - positionB.x()) : 0.0f;
        const float rBy = activeB ? (py - positionB.y()) : 0.0f;

        const float rnA = rAx * nY - rAy * nX;
        const float rnB = rBx * nY - rBy * nX;
        const float rtA = rAx * nX + rAy * nY;
        const float rtB = rBx * nX + rBy * nY;
        outConstraint.rnA[i] = rnA;
        outConstraint.rnB[i] = rnB;
        outConstraint.rtA[i] = rtA;
        outConstraint.rtB[i] = rtB;

        const float kNormal = inverseMassSum + invInertiaA * rnA * rnA + invInertiaB * rnB * rnB;
        outConstraint.massNormal[i] = kNormal > 0.0f ? 1.0f / kNormal : 0.0f;

        const float kTangent = inverseMassSum + invInertiaA * rtA * rtA + invInertiaB * rtB * rtB;
        outConstraint.massTangent[i] = kTangent > 0.0f ? 1.0f / kTangent : 0.0f;

        outConstraint.baseSeparation[i] = cp.separation;
        outConstraint.relativeVelocity[i] = combinedRestitution > 0.0f ? (dvN + angVelB * rnB - angVelA * rnA) : 0.0f;
        outConstraint.accumulatedNormalImpulse[i] = cp.accumulatedNormalImpulse;
        outConstraint.accumulatedTangentImpulse[i] = cp.accumulatedTangentImpulse;
    }
}

void PhysicsManager::applyWarmStart(const SolverConstraint &constraint, SolverBodyState &bodyA,
                                    SolverBodyState &bodyB) noexcept
{
    const float invMassA = bodyA.inverseMass;
    const float invInertiaA = bodyA.inverseMomentOfInertia;
    const float invMassB = bodyB.inverseMass;
    const float invInertiaB = bodyB.inverseMomentOfInertia;

    float sumN = 0.0f;
    float sumT = 0.0f;
    float angA = 0.0f;
    float angB = 0.0f;
    for (uint8_t i = 0; i < constraint.pointCount; ++i)
    {
        const float aN = constraint.accumulatedNormalImpulse[i];
        const float aT = constraint.accumulatedTangentImpulse[i];
        sumN += aN;
        sumT += aT;
        angA += aN * constraint.rnA[i] + aT * constraint.rtA[i];
        angB += aN * constraint.rnB[i] + aT * constraint.rtB[i];
    }

    const float nX = constraint.normal.x();
    const float nY = constraint.normal.y();
    const float impX = sumN * nX - sumT * nY;
    const float impY = sumN * nY + sumT * nX;

    if (invMassA != 0.0f || invInertiaA != 0.0f)
    {
        bodyA.vx -= invMassA * impX;
        bodyA.vy -= invMassA * impY;
        bodyA.w -= invInertiaA * angA;
    }
    if (invMassB != 0.0f || invInertiaB != 0.0f)
    {
        bodyB.vx += invMassB * impX;
        bodyB.vy += invMassB * impY;
        bodyB.w += invInertiaB * angB;
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
    const float invMassSum = invMassA + invMassB;

    const float nX = constraint.normal.x();
    const float nY = constraint.normal.y();
    const float dvX = bodyB.vx - bodyA.vx;
    const float dvY = bodyB.vy - bodyA.vy;
    float dvN = dvX * nX + dvY * nY;
    float dvT = dvY * nX - dvX * nY;
    float wA = bodyA.w;
    float wB = bodyB.w;
    float dpN = 0.0f;
    if constexpr (UseBias)
    {
        dpN = (bodyB.dpX - bodyA.dpX) * nX + (bodyB.dpY - bodyA.dpY) * nY;
    }

    float totalN = 0.0f;
    float totalT = 0.0f;
    for (uint8_t i = 0; i < constraint.pointCount; ++i)
    {
        const float rnA = constraint.rnA[i];
        const float rnB = constraint.rnB[i];
        const float vn = dvN + wB * rnB - wA * rnA;

        float normalImpulse = 0.0f;
        if constexpr (UseBias)
        {
            const float sep = constraint.baseSeparation[i] + dpN + bodyB.dAngle * rnB - bodyA.dAngle * rnA;
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
        totalN += appliedNormal;
        dvN += invMassSum * appliedNormal;
        wA -= invInertiaA * rnA * appliedNormal;
        wB += invInertiaB * rnB * appliedNormal;

        const float rtA = constraint.rtA[i];
        const float rtB = constraint.rtB[i];
        const float vt = dvT + wB * rtB - wA * rtA;
        const float rawTangent = -constraint.massTangent[i] * vt;

        const float oldTangent = constraint.accumulatedTangentImpulse[i];
        const float maxTangent = constraint.solverFriction * newNormal;
        const float newTangent = std::clamp(oldTangent + rawTangent, -maxTangent, maxTangent);
        constraint.accumulatedTangentImpulse[i] = newTangent;
        const float appliedTangent = newTangent - oldTangent;
        totalT += appliedTangent;
        dvT += invMassSum * appliedTangent;
        wA -= invInertiaA * rtA * appliedTangent;
        wB += invInertiaB * rtB * appliedTangent;
    }

    const float impX = totalN * nX - totalT * nY;
    const float impY = totalN * nY + totalT * nX;
    if (invMassA != 0.0f || invInertiaA != 0.0f)
    {
        bodyA.vx -= invMassA * impX;
        bodyA.vy -= invMassA * impY;
        bodyA.w = wA;
    }
    if (invMassB != 0.0f || invInertiaB != 0.0f)
    {
        bodyB.vx += invMassB * impX;
        bodyB.vy += invMassB * impY;
        bodyB.w = wB;
    }
}

template void PhysicsManager::solveConstraintSoft<true>(SolverConstraint &, SolverBodyState &, SolverBodyState &,
                                                        const SoftStepParams &) noexcept;
template void PhysicsManager::solveConstraintSoft<false>(SolverConstraint &, SolverBodyState &, SolverBodyState &,
                                                         const SoftStepParams &) noexcept;

void PhysicsManager::applyRestitutionSoft(const SolverConstraint &constraint, SolverBodyState &bodyA,
                                          SolverBodyState &bodyB, const SoftStepParams &params) noexcept
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
    const float invMassSum = invMassA + invMassB;

    const float nX = constraint.normal.x();
    const float nY = constraint.normal.y();
    float dvN = (bodyB.vx - bodyA.vx) * nX + (bodyB.vy - bodyA.vy) * nY;
    float wA = bodyA.w;
    float wB = bodyB.w;
    float totalN = 0.0f;
    bool changed = false;

    for (uint8_t i = 0; i < constraint.pointCount; ++i)
    {
        const float relVel = constraint.relativeVelocity[i];
        if (relVel >= -params.restitutionThreshold)
        {
            continue;
        }
        const float rnA = constraint.rnA[i];
        const float rnB = constraint.rnB[i];
        const float vn = dvN + wB * rnB - wA * rnA;
        const float applied = std::max(constraint.massNormal[i] * (-restitution * relVel - vn), 0.0f);
        if (applied <= 0.0f)
        {
            continue;
        }
        totalN += applied;
        dvN += invMassSum * applied;
        wA -= invInertiaA * rnA * applied;
        wB += invInertiaB * rnB * applied;
        changed = true;
    }

    if (!changed)
    {
        return;
    }
    const float impX = totalN * nX;
    const float impY = totalN * nY;
    if (invMassA != 0.0f || invInertiaA != 0.0f)
    {
        bodyA.vx -= invMassA * impX;
        bodyA.vy -= invMassA * impY;
        bodyA.w = wA;
    }
    if (invMassB != 0.0f || invInertiaB != 0.0f)
    {
        bodyB.vx += invMassB * impX;
        bodyB.vy += invMassB * impY;
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
