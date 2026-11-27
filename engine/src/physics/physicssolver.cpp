#include "physics/physicsmanager.h"

#include "benchmark/benchmark.h"
#include "core/constants.h"
#include "core/gamemanager.h"
#include "core/runtimeaccess.h"
#include "oneapi/tbb.h"
#include "benchmark/workprobe.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr size_t parallelSolverBatchSize = 128;
constexpr size_t parallelPreStepThreshold = 256;

bool hasWakeMotion(const Rigidbody *other) noexcept
{
    return other->getVelocity().squaredNorm() > LINEAR_SLEEP_THRESHOLD * LINEAR_SLEEP_THRESHOLD ||
           std::abs(other->getAngularVelocity()) > ANGULAR_SLEEP_THRESHOLD ||
           (other->getBodyType() == KINEMATIC &&
            (!other->getVelocity().isZero(0) || other->getAngularVelocity() != 0));
}
}

void PhysicsManager::resolveCollisions(double timeDelta)
{
    PLATFORMATOR_BENCH_SCOPE(ResolveCollisions);

    const float inverseTimeDelta = timeDelta > 0.0 ? static_cast<float>(1.0 / timeDelta) : 0.0f;
    solverPreStepJobs.clear();
    if (solverPreStepJobs.capacity() < solverPairs.size())
    {
        solverPreStepJobs.reserve(solverPairs.size());
    }

    for (auto *pair : solverPairs)
    {
        auto *collision = pair->getCollision();
        const Collision::CollisionType resolutionType = collision->getResolutionType();

        switch (resolutionType)
        {
        case Collision::CollisionType::DYNAMIC: {
            auto *rbA = static_cast<Rigidbody *>(
                collision->getReferenceObject()->getGameObject()->getComponent(ComponentType::RIGID_BODY));
            auto *rbB = static_cast<Rigidbody *>(
                collision->getIncidentObject()->getGameObject()->getComponent(ComponentType::RIGID_BODY));
            if (rbA == nullptr || rbB == nullptr)
            {
                break;
            }
            if (rbA->getIsSleeping() && hasWakeMotion(rbB))
            {
                rbA->wakeUp();
            }
            if (rbB->getIsSleeping() && hasWakeMotion(rbA))
            {
                rbB->wakeUp();
            }
            mergeSleepIslands(rbA, rbB);
            solverPreStepJobs.push_back(SolverPreStepJob{collision, rbA, rbB});
            break;
        }
        case Collision::CollisionType::KINEMATIC:
            resolveKinematicCollision(collision);
            break;
        case Collision::CollisionType::TRIGGER:
            break;
        default:
            break;
        }
    }

    const size_t constraintCount = solverPreStepJobs.size();
    solverConstraints.resize(constraintCount);
    if (constraintCount < parallelPreStepThreshold ||
        tbb::global_control::active_value(tbb::global_control::max_allowed_parallelism) <= 1 ||
        tbb::this_task_arena::max_concurrency() <= 1)
    {
        for (size_t i = 0; i < constraintCount; ++i)
        {
            prepareSolverConstraint(solverPreStepJobs[i], inverseTimeDelta, solverConstraints[i]);
        }
    }
    else
    {
        tbb::task_group_context context(tbb::task_group_context::isolated);
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

    solveConstraintIterations();

    reconcileChangedBodies();
    updateSleepingStates(timeDelta);

#if PLATFORMATOR_ENABLE_BENCHMARKS
    PLATFORMATOR_BENCH_SET_COUNTER(AwakeDynamicBodyCount, countAwakeDynamicBodies());
#endif
}

void PhysicsManager::solveConstraintIterations()
{
    // For small batches, the serial loop is cheaper than building and dispatching islands.
    constexpr size_t parallelConstraintThreshold = 256;
    constexpr size_t orderedConstraintThreshold = 512;
    if (solverConstraints.size() >= parallelConstraintThreshold &&
        tbb::global_control::active_value(tbb::global_control::max_allowed_parallelism) > 1 &&
        tbb::this_task_arena::max_concurrency() > 1 && prepareSolverIslands())
    {
        if (largestSolverIsland <= solverConstraints.size() / 2)
        {
            solveIslands();
            return;
        }
        if (largestSolverIsland >= orderedConstraintThreshold && prepareOrderedSolverBatches())
        {
            solveOrderedBatches();
            return;
        }
    }

    solveConstraintsSerial();
}

void PhysicsManager::solveConstraintsSerial()
{
    for (const SolverConstraint &constraint : solverConstraints)
    {
        applyWarmStart(constraint);
    }
    for (size_t iteration = 0; iteration < COLLISION_SOLVER_ITERATIONS; ++iteration)
    {
        for (const SolverConstraint &constraint : solverConstraints)
        {
            resolveCollision(constraint);
        }
    }
}

bool PhysicsManager::prepareSolverIslands()
{
    // Two endpoints per constraint give an upper bound on the bodies this graph can touch.
    sparseSolverBodies = registeredBodies.size() / 4 > solverConstraints.size();
    if (sparseSolverBodies)
    {
        // Clear the previous markers before resizing. Registered slots may have been reassigned.
        for (const auto &[a, b] : solverEndpoints)
        {
            if (a != SIZE_MAX)
                solverBodyNodes[a].rank = 0;
            if (b != SIZE_MAX)
                solverBodyNodes[b].rank = 0;
        }
    }
    solverBodyNodes.resize(registeredBodies.size());
    if (!sparseSolverBodies)
    {
        for (size_t i = 0; i < solverBodyNodes.size(); ++i)
            solverBodyNodes[i] = {i, 0, SIZE_MAX};
    }
    solverEndpoints.clear();
    solverEndpoints.reserve(solverConstraints.size());
    solverIslandCount = 0;
    largestSolverIsland = 0;

    // Match applySolverImpulse after preparation. Sleeping, static, and kinematic bodies can be shared reads.
    // A writable body still awaiting registration cannot be indexed by this schedule.
    const auto getWritableIndex = [&](const Rigidbody *body, size_t &index) {
        index = SIZE_MAX;
        if (body->getBodyType() != DYNAMIC || body->getIsSleeping())
            return true;
        index = body->getPhysicsManagerIndex();
        return index < registeredBodies.size() && registeredBodies[index] == body;
    };
    const auto rootFor = [&](size_t index) {
        while (solverBodyNodes[index].parent != index)
        {
            solverBodyNodes[index].parent = solverBodyNodes[solverBodyNodes[index].parent].parent;
            index = solverBodyNodes[index].parent;
        }
        return index;
    };
    for (const auto &constraint : solverConstraints)
    {
        size_t a, b;
        if (!getWritableIndex(constraint.rbA, a) || !getWritableIndex(constraint.rbB, b))
            return false;
        solverEndpoints.emplace_back(a, b);
        if (sparseSolverBodies)
        {
            // Use a nonzero rank to mark every initialized endpoint, including singleton roots.
            if (a != SIZE_MAX && solverBodyNodes[a].rank == 0)
                solverBodyNodes[a] = {a, 1, SIZE_MAX};
            if (b != SIZE_MAX && solverBodyNodes[b].rank == 0)
                solverBodyNodes[b] = {b, 1, SIZE_MAX};
        }
        if (a == SIZE_MAX || b == SIZE_MAX)
            continue;

        a = rootFor(a);
        b = rootFor(b);
        if (a == b)
            continue;
        if (solverBodyNodes[a].rank < solverBodyNodes[b].rank)
            std::swap(a, b);
        solverBodyNodes[b].parent = a;
        if (solverBodyNodes[a].rank == solverBodyNodes[b].rank)
            ++solverBodyNodes[a].rank;
    }

    size_t readOnlyIsland = SIZE_MAX;
    for (size_t i = 0; i < solverEndpoints.size(); ++i)
    {
        const auto [a, b] = solverEndpoints[i];
        const size_t body = a != SIZE_MAX ? a : b;
        auto &island = body != SIZE_MAX ? solverBodyNodes[rootFor(body)].island : readOnlyIsland;
        if (island == SIZE_MAX)
        {
            island = solverIslandCount++;
            if (island == solverIslands.size())
                solverIslands.emplace_back();
            solverIslands[island].clear();
        }
        auto &order = solverIslands[island];
        order.push_back(i);
        largestSolverIsland = std::max(largestSolverIsland, order.size());
    }
    return true;
}

void PhysicsManager::solveIslands()
{
    const auto workers = std::min(tbb::global_control::active_value(tbb::global_control::max_allowed_parallelism),
                                  static_cast<size_t>(tbb::this_task_arena::max_concurrency()));
    const size_t target = std::max(size_t{1}, solverConstraints.size() / workers / 4);
    solverIslandJobEnds.clear();
    size_t jobSize = 0;
    for (size_t island = 0; island < solverIslandCount; ++island)
    {
        const size_t count = solverIslands[island].size();
        if (jobSize != 0 && jobSize + count > target)
        {
            solverIslandJobEnds.push_back(island);
            jobSize = 0;
        }
        jobSize += count;
        PLATFORMATOR_WORK_SET(solverLargestIslandJob, std::max(platformator_test::work.solverLargestIslandJob, jobSize));
    }
    solverIslandJobEnds.push_back(solverIslandCount);
    PLATFORMATOR_WORK_ADD(solverIslandJobs, solverIslandJobEnds.size());

    // A simulation step must finish applying impulses even if the caller's task group is cancelled.
    tbb::task_group_context context(tbb::task_group_context::isolated);
    tbb::parallel_for(size_t{0}, solverIslandJobEnds.size(), [&](size_t job) {
        auto &local = solverIslandLocalConstraints.local();
        const size_t begin = job == 0 ? 0 : solverIslandJobEnds[job - 1];
        for (size_t island = begin; island < solverIslandJobEnds[job]; ++island)
        {
            const auto &indices = solverIslands[island];
            local.clear();
            if (local.capacity() < indices.size())
            {
                local.reserve(indices.size());
            }
            for (size_t index : indices)
            {
                local.push_back(solverConstraints[index]);
                applyWarmStart(local.back());
            }
            for (size_t iteration = 0; iteration < COLLISION_SOLVER_ITERATIONS; ++iteration)
                for (const SolverConstraint &constraint : local)
                    resolveCollision(constraint);
        }
    }, context);
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
        const size_t batch = std::max(a != SIZE_MAX ? solverBodyNextBatch[a] : 0,
                                      b != SIZE_MAX ? solverBodyNextBatch[b] : 0);
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
    return parallelConstraints >= solverConstraints.size() / 2;
}

void PhysicsManager::solveOrderedBatches()
{
    PLATFORMATOR_WORK_ADD(solverOrderedBatchDispatches, 1);
    tbb::task_group_context context(tbb::task_group_context::isolated);
    for (size_t batch = 0; batch < solverBatchCount; ++batch)
    {
        const auto &order = solverBatches[batch];
        if (order.size() < parallelSolverBatchSize)
        {
            for (size_t index : order)
                applyWarmStart(solverConstraints[index]);
        }
        else
        {
            tbb::parallel_for(tbb::blocked_range<size_t>(0, order.size(), 32), [&](const auto &range) {
                for (size_t i = range.begin(); i != range.end(); ++i)
                    applyWarmStart(solverConstraints[order[i]]);
            }, context);
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
                    resolveCollision(solverConstraints[index]);
            }
            else
            {
                tbb::parallel_for(tbb::blocked_range<size_t>(0, order.size(), 32), [&](const auto &range) {
                    for (size_t i = range.begin(); i != range.end(); ++i)
                        resolveCollision(solverConstraints[order[i]]);
                }, context);
            }
        }
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

    const Eigen::Vector2f positionA = rbA->getGameObject()->worldPosition;
    const Eigen::Vector2f positionB = rbB->getGameObject()->worldPosition;

    const Eigen::Vector2f &normal = collision->getNormal();
    const Eigen::Vector2f tangent = crossSV(1.0f, normal);
    const float inverseMassSum = invMassA + invMassB;
    ClipPointsWithData &contactPoints = collision->getContactPoints();
    const uint8_t pointCount = static_cast<uint8_t>(std::min<size_t>(contactPoints.count, 2));

    outConstraint.rbA = rbA;
    outConstraint.rbB = rbB;
    outConstraint.contactPoints = &contactPoints;
    outConstraint.normal = normal;
    outConstraint.tangent = tangent;
    outConstraint.solverFriction = collision->getFriction();
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

    for (uint8_t i = 0; i < pointCount; ++i)
    {
        const Eigen::Vector2f &rA = outConstraint.rA[i];
        const Eigen::Vector2f &rB = outConstraint.rB[i];
        const float rnA = rA.dot(normal);
        const float rnB = rB.dot(normal);
        const float kNormal =
            inverseMassSum + invInertiaA * (rA.dot(rA) - rnA * rnA) + invInertiaB * (rB.dot(rB) - rnB * rnB);
        contactPoints.points[i].massNormal = kNormal > 0 ? 1.0f / kNormal : 0.f;

        const float rtA = rA.dot(tangent);
        const float rtB = rB.dot(tangent);
        const float kTangent =
            inverseMassSum + invInertiaA * (rA.dot(rA) - rtA * rtA) + invInertiaB * (rB.dot(rB) - rtB * rtB);
        contactPoints.points[i].massTangent = kTangent > 0 ? 1.0f / kTangent : 0.f;

        const float baumgarteBias =
            -COLLISION_BIAS_FACTOR * inverseTimeDelta *
            std::min(0.0f, contactPoints.points[i].separation + COLLISION_ALLOWED_PENETRATION);

        const Eigen::Vector2f relativeVelocity = (rbB->getVelocity() + crossSV(rbB->getAngularVelocity(), rB)) -
                                                 (rbA->getVelocity() + crossSV(rbA->getAngularVelocity(), rA));
        const float vn = relativeVelocity.dot(normal);
        float restitutionBias = 0.0f;
        if (vn < -RESTITUTION_VELOCITY_THRESHOLD)
        {
            restitutionBias = -collision->getRestitution() * vn;
        }

        contactPoints.points[i].bias = baumgarteBias + restitutionBias;
    }
}

void PhysicsManager::applyWarmStart(const SolverConstraint &constraint)
{
    Rigidbody *rbA = constraint.rbA;
    Rigidbody *rbB = constraint.rbB;
    const ClipPointsWithData &contactPoints = *constraint.contactPoints;
    const Eigen::Vector2f &normal = constraint.normal;
    const Eigen::Vector2f &tangent = constraint.tangent;
    for (uint8_t i = 0; i < constraint.pointCount; ++i)
    {
        const Eigen::Vector2f accumulatedImpulse = contactPoints.points[i].accumulatedNormalImpulse * normal +
                                                   contactPoints.points[i].accumulatedTangentImpulse * tangent;
        rbA->applySolverImpulse(-accumulatedImpulse, constraint.rA[i]);
        rbB->applySolverImpulse(accumulatedImpulse, constraint.rB[i]);
    }
}

void PhysicsManager::resolveCollision(const SolverConstraint &constraint)
{
    Rigidbody *rbA = constraint.rbA;
    Rigidbody *rbB = constraint.rbB;
    ClipPointsWithData &contactPoints = *constraint.contactPoints;
    const Eigen::Vector2f &normal = constraint.normal;
    const Eigen::Vector2f &tangent = constraint.tangent;

    if (constraint.blockSolve)
    {
        const Eigen::Vector2f previous(contactPoints.points[0].accumulatedNormalImpulse,
                                       contactPoints.points[1].accumulatedNormalImpulse);
        Eigen::Vector2f rhs;
        for (int i = 0; i < 2; ++i)
        {
            rhs[i] = ((rbB->getVelocity() + crossSV(rbB->getAngularVelocity(), constraint.rB[i])) -
                      (rbA->getVelocity() + crossSV(rbA->getAngularVelocity(), constraint.rA[i])))
                         .dot(normal) -
                     contactPoints.points[i].bias;
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
                rbA->applySolverImpulse(-impulse, constraint.rA[i]);
                rbB->applySolverImpulse(impulse, constraint.rB[i]);
                contactPoints.points[i].accumulatedNormalImpulse = next[i];
            }
        }
    }

    for (uint8_t i = 0; i < constraint.pointCount; ++i)
    {
        const Eigen::Vector2f &rA = constraint.rA[i];
        const Eigen::Vector2f &rB = constraint.rB[i];

        Eigen::Vector2f relativeVelocity = (rbB->getVelocity() + crossSV(rbB->getAngularVelocity(), rB)) -
                                           (rbA->getVelocity() + crossSV(rbA->getAngularVelocity(), rA));

        if (!constraint.blockSolve)
        {
            float vn = relativeVelocity.dot(normal);
            float normalImpulse = contactPoints.points[i].massNormal * (-vn + contactPoints.points[i].bias);

            float accumulatedNormalImpulse = contactPoints.points[i].accumulatedNormalImpulse;
            float maxNormalImpulse = std::max(accumulatedNormalImpulse + normalImpulse, 0.0f);
            contactPoints.points[i].accumulatedNormalImpulse = maxNormalImpulse;
            normalImpulse = maxNormalImpulse - accumulatedNormalImpulse;

            Eigen::Vector2f impulse = normalImpulse * normal;

            rbA->applySolverImpulse(-impulse, rA);
            rbB->applySolverImpulse(impulse, rB);
        }

        relativeVelocity = (rbB->getVelocity() + crossSV(rbB->getAngularVelocity(), rB)) -
                           (rbA->getVelocity() + crossSV(rbA->getAngularVelocity(), rA));

        float vt = relativeVelocity.dot(tangent);
        float tangentImpulse = contactPoints.points[i].massTangent * (-vt);

        float oldTangentImpulse = contactPoints.points[i].accumulatedTangentImpulse;

        float maxTangentImpulse =
            constraint.solverFriction * contactPoints.points[i].accumulatedNormalImpulse;
        float newTangentImpulse =
            Clamp(oldTangentImpulse + tangentImpulse, -maxTangentImpulse, maxTangentImpulse);
        contactPoints.points[i].accumulatedTangentImpulse = newTangentImpulse;
        tangentImpulse = newTangentImpulse - oldTangentImpulse;

        Eigen::Vector2f contactImpulse = tangentImpulse * tangent;

        rbA->applySolverImpulse(-contactImpulse, rA);
        rbB->applySolverImpulse(contactImpulse, rB);
    }
}

void PhysicsManager::resolveKinematicCollision(const Collision *collision)
{
    Rigidbody *rbA = (Rigidbody *)collision->getReferenceObject()->getGameObject()->getComponent(
        ComponentType::RIGID_BODY);
    Rigidbody *rbB =
        (Rigidbody *)collision->getIncidentObject()->getGameObject()->getComponent(ComponentType::RIGID_BODY);

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

void PhysicsManager::resolveKinematicBodyAgainstNormal(Rigidbody *rigidBody,
                                                       const Eigen::Vector2f &approachNormal,
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

