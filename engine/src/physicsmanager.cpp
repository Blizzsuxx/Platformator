#include "physicsmanager.h"
#include "physicsgeometry.h"

#include "benchmark.h"
#include "constants.h"
#include "debugdraw.h"
#include "gamemanager.h"
#include "oneapi/tbb.h"
#include "platformator/boxcollider.h"
#include "platformator/circlecollider.h"
#include "runtimeaccess.h"
#include "workprobe.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>
#include <tuple>

namespace
{
constexpr size_t parallelSolverBatchSize = 128;
}

PhysicsManager::PhysicsManager()
    : registeredBodies(), colliderComponents(), pendingPhysicsEvents(), grid(this),
      gravityVector(GRAVITY_VECTOR_X, GRAVITY_VECTOR_Y), gravityVectorNormalized(gravityVector.normalized())
{
}

PhysicsManager::~PhysicsManager()
{
}

void PhysicsManager::applyPhysics(double timeDelta)
{
    PLATFORMATOR_WORK_ADD(forceBodies, awakeDynamicBodies.size());
    if (awakeDynamicBodies.empty())
    {
        return;
    }
    const auto integrateBody = [&](Rigidbody *body) {
        resetSleepIsland(body);
        body->integrateForces(timeDelta, gravityVector);
    };
    if (awakeDynamicBodies.size() < BODY_INTEGRATION_PARALLEL_THRESHOLD)
    {
        for (auto *body : awakeDynamicBodies)
        {
            integrateBody(body);
        }
        return;
    }
    tbb::parallel_for(size_t(0), awakeDynamicBodies.size(), [&](size_t i) { integrateBody(awakeDynamicBodies[i]); });
}

void PhysicsManager::applyMovement(double timeDelta)
{
    const size_t totalMoving = awakeDynamicBodies.size() + movingKinematicBodies.size();
    PLATFORMATOR_WORK_ADD(movingBodies, totalMoving);
    if (totalMoving == 0)
    {
        return;
    }
    auto &manager = platformator_detail::RuntimeAccess::gameManager();
    const auto publishMoved = [&](GameObject *object, bool translated, bool rotated) {
        const bool handledBySubtreeSync =
            translated && object->parent == nullptr && object->position != object->worldPosition;
        if (rotated && !handledBySubtreeSync)
        {
            manager.publishRenderGeometryChange(object);
            object->updateCollider();
        }
        if (translated)
        {
            // The manager synchronizes this batch before any callbacks or reads.
            manager.queueTransform(object, false);
        }
    };
    if (totalMoving < BODY_INTEGRATION_PARALLEL_THRESHOLD)
    {
        for (auto *body : awakeDynamicBodies)
        {
            if (const auto changes = body->integrateLocal(timeDelta))
            {
                publishMoved(body->getGameObject(), (changes & 1) != 0, (changes & 2) != 0);
            }
        }
        for (auto *body : movingKinematicBodies)
        {
            if (const auto changes = body->integrateLocal(timeDelta))
            {
                publishMoved(body->getGameObject(), (changes & 1) != 0, (changes & 2) != 0);
            }
        }
        return;
    }
    movedBodyChanges.resize(totalMoving);
    tbb::parallel_for(size_t(0), totalMoving, [&](size_t i) {
        auto *body =
            i < awakeDynamicBodies.size() ? awakeDynamicBodies[i] : movingKinematicBodies[i - awakeDynamicBodies.size()];
        movedBodyChanges[i] = static_cast<uint8_t>(body->integrateLocal(timeDelta));
    });
    for (size_t i = 0; i < totalMoving; ++i)
    {
        const uint8_t changes = movedBodyChanges[i];
        if (changes != 0)
        {
            auto *body = i < awakeDynamicBodies.size() ? awakeDynamicBodies[i]
                                                       : movingKinematicBodies[i - awakeDynamicBodies.size()];
            publishMoved(body->getGameObject(), (changes & 1) != 0, (changes & 2) != 0);
        }
    }
}

void PhysicsManager::checkForCollisions()
{
    broadPhase();
    narrowPhase();
}

const Grid &PhysicsManager::getGrid() const
{
    return grid;
}

const std::vector<Collider *> &PhysicsManager::getColliders() const
{
    return colliderComponents;
}

void PhysicsManager::addRigidBodyComponent(Rigidbody *rigidBodyComponent)
{
    if (rigidBodyComponent == nullptr || rigidBodyComponent->getIsRegisteredInPhysicsManager() ||
        !rigidBodyComponent->getGameObject()->getActive())
    {
        return;
    }

    rigidBodyComponent->setPhysicsManagerIndex(registeredBodies.size());
    rigidBodyComponent->setIsRegisteredInPhysicsManager(true);
    registeredBodies.push_back(rigidBodyComponent);
    queueBodyParticipationChange(rigidBodyComponent);
}

void PhysicsManager::addColliderComponent(Collider *colliderComponent)
{
    if (colliderComponent == nullptr || colliderComponent->getIsRegisteredInPhysicsManager() ||
        !colliderComponent->getGameObject()->getActive())
    {
        return;
    }

    colliderComponent->setPhysicsManagerIndex(colliderComponents.size());
    colliderComponent->setIsRegisteredInPhysicsManager(true);
    colliderComponents.push_back(colliderComponent);

    grid.queueAddCollider(colliderComponent);
}

void PhysicsManager::queueColliderSync(Collider *colliderComponent)
{
    grid.queueSyncCollider(colliderComponent);
}

void PhysicsManager::removeRigidBodyComponent(Rigidbody *rigidBodyComponent)
{
    if (rigidBodyComponent == nullptr || !rigidBodyComponent->getIsRegisteredInPhysicsManager())
    {
        return;
    }

    size_t removeIndex = rigidBodyComponent->getPhysicsManagerIndex();
    size_t lastIndex = registeredBodies.size() - 1;

    if (removeIndex != lastIndex)
    {
        Rigidbody *movedRigidBody = registeredBodies[lastIndex];
        registeredBodies[removeIndex] = movedRigidBody;
        movedRigidBody->setPhysicsManagerIndex(removeIndex);
    }

    registeredBodies.pop_back();
    rigidBodyComponent->setPhysicsManagerIndex(SIZE_MAX);
    rigidBodyComponent->setIsRegisteredInPhysicsManager(false);
    cancelBodyParticipationChange(rigidBodyComponent);
    reconcileBodyParticipation(rigidBodyComponent);
}

void PhysicsManager::removeColliderComponent(Collider *colliderComponent)
{
    if (colliderComponent == nullptr || !colliderComponent->getIsRegisteredInPhysicsManager())
    {
        return;
    }

    size_t removeIndex = colliderComponent->getPhysicsManagerIndex();
    size_t lastIndex = colliderComponents.size() - 1;

    if (removeIndex != lastIndex)
    {
        Collider *movedCollider = colliderComponents[lastIndex];
        colliderComponents[removeIndex] = movedCollider;
        movedCollider->setPhysicsManagerIndex(removeIndex);
    }

    colliderComponents.pop_back();
    colliderComponent->setPhysicsManagerIndex(SIZE_MAX);
    colliderComponent->setIsRegisteredInPhysicsManager(false);

    grid.queueRemoveCollider(colliderComponent);
}

void PhysicsManager::flushPendingColliderOperations()
{
    {
        PLATFORMATOR_BENCH_SCOPE(DeferredGridFlush);
        grid.flushPendingCellUpdates();
    }
    flushSupportDeltas();
}

void PhysicsManager::clearAll()
{
    for (Rigidbody *body : registeredBodies)
    {
        body->setIsRegisteredInPhysicsManager(false);
        body->setPhysicsManagerIndex(SIZE_MAX);
        body->awakeDynamicIndex = SIZE_MAX;
        body->movingKinematicIndex = SIZE_MAX;
        body->sleepCandidateIndex = SIZE_MAX;
        body->participationQueueIndex = SIZE_MAX;
        body->supportDeltaIndex = SIZE_MAX;
        body->supportDelta = 0;
    }
    for (Rigidbody *body : pendingBodyChanges)
    {
        body->participationQueueIndex = SIZE_MAX;
    }
    for (Rigidbody *body : supportDeltaBodies)
    {
        body->supportDeltaIndex = SIZE_MAX;
        body->supportDelta = 0;
    }
    for (Collider *collider : colliderComponents)
    {
        collider->setIsRegisteredInPhysicsManager(false);
        collider->setPhysicsManagerIndex(SIZE_MAX);
        collider->setIsRegisteredInGrid(false);
        collider->desiredInGrid = false;
        collider->gridQueueIndex = SIZE_MAX;
        collider->cachedCellCount = 0;
        collider->adjacentPairs.clear();
    }

    registeredBodies.clear();
    awakeDynamicBodies.clear();
    movingKinematicBodies.clear();
    sleepCandidates.clear();
    pendingBodyChanges.clear();
    colliderComponents.clear();
    solverConstraints.clear();
    contactPairs.clear();
    solverPairs.clear();
    supportDeltaBodies.clear();
    pendingPhysicsEvents.clear();
    movedBodyChanges.clear();
    grid.clearAll();
}

void PhysicsManager::broadPhase()
{
    PLATFORMATOR_BENCH_SCOPE(BroadPhase);

    grid.flushPendingCellUpdates();

    PLATFORMATOR_BENCH_SET_COUNTER(OccupiedCellCount, grid.getCells().size());
    PLATFORMATOR_BENCH_SET_COUNTER(CandidatePairCount, grid.getCandidatePairCount());
}

void PhysicsManager::narrowPhase()
{
    PLATFORMATOR_BENCH_SCOPE(NarrowPhase);

    const auto &pendingPairs = *grid.getPendingNarrowPhasePairs();
    if (pendingPairs.size() < NARROW_PHASE_PARALLEL_THRESHOLD)
    {
        for (auto *pair : pendingPairs)
        {
            processNarrowPhasePair(pair);
        }
    }
    else
    {
        tbb::parallel_for(size_t(0), pendingPairs.size(), [&](size_t i)
                          { processNarrowPhasePair(pendingPairs[i]); });
    }

    for (auto *pair : pendingPairs)
    {
        PLATFORMATOR_WORK_ADD(geometryPairs, pair->resultTestedGeometry ? 1 : 0);
        PLATFORMATOR_WORK_ADD(contactTransitionChecks, 1);
        auto *collision = pair->getCollision();
        const bool hasContact = collision != nullptr;
        if (pair->resultHadContact != hasContact)
        {
            const auto type = hasContact ? PhysicsEvent::COLLISION_ENTER : PhysicsEvent::COLLISION_EXIT;
            addPendingPhysicsEvent(makePhysicsEvent(type, pair, collision));
        }
        else if (pair->solverIndex == SIZE_MAX && !pair->contributedSupportBodyA && !pair->contributedSupportBodyB &&
                 (pair->contactIndex != SIZE_MAX) == hasContact)
        {
            // A surviving trigger is already live after the geometry pass and can neither support nor solve.
            if (!hasContact || pair->getObjectA()->getIsTrigger() || pair->getObjectB()->getIsTrigger())
                continue;
        }

        reconcilePairSupport(pair);
        refreshContactParticipation(pair);
    }
    flushSupportDeltas();

#if PLATFORMATOR_ENABLE_DEBUG_TOOLS
    for (auto *pair : solverPairs)
    {
        auto *collision = pair->getCollision();
        DebugDraw::getInstance().addCollisionDebugObject(*collision);
    }
#endif

#if PLATFORMATOR_ENABLE_BENCHMARKS
    PLATFORMATOR_BENCH_SET_COUNTER(PendingNarrowPhasePairCount, pendingPairs.size());
    PLATFORMATOR_BENCH_SET_COUNTER(ActiveCollisionCount, contactPairs.size());

#endif

    grid.clearPendingNarrowPhasePairs();
}

#if PLATFORMATOR_ENABLE_BENCHMARKS
size_t PhysicsManager::countAwakeDynamicBodies() const
{
    return awakeDynamicBodies.size();
}
#endif

void PhysicsManager::processNarrowPhasePair(const ColliderPair *pair)
{
    // Structural queue mutation/erasure cannot overlap this distinct-pair worker batch.
    pair->setNarrowPhaseQueueIndex(SIZE_MAX);
    pair->resultHadContact = pair->getCollision() != nullptr;
#if PLATFORMATOR_ENABLE_WORK_PROBES
    pair->resultTestedGeometry = false;
#endif
    if (!pair->getObjectA()->getGameObject()->getActive() ||
        !pair->getObjectB()->getGameObject()->getActive() || pair->getObjectA()->isRetired() ||
        pair->getObjectB()->isRetired())
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

void PhysicsManager::resolveCollisions(double timeDelta)
{
    PLATFORMATOR_BENCH_SCOPE(ResolveCollisions);

    float inverseTimeDelta = timeDelta > 0.0 ? static_cast<float>(1.0 / timeDelta) : 0.0f;
    solverConstraints.clear();
    if (solverConstraints.capacity() < solverPairs.size())
    {
        solverConstraints.reserve(solverPairs.size());
    }

    for (auto *pair : solverPairs)
    {
        auto *collision = pair->getCollision();
        Collision::CollisionType resolutionType = collision->getResolutionType();

        switch (resolutionType)
        {
        case Collision::CollisionType::DYNAMIC: {
            SolverConstraint constraint;
            if (preStepCollision(collision, inverseTimeDelta, constraint))
            {
                solverConstraints.push_back(constraint);
            }
            break;
        }
        case Collision::CollisionType::KINEMATIC:
            resolveKinematicCollision(collision);
            break;
        case Collision::CollisionType::TRIGGER:
            /* code */
            break;
        default:
            break;
        }
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
        const size_t begin = job == 0 ? 0 : solverIslandJobEnds[job - 1];
        for (size_t island = begin; island < solverIslandJobEnds[job]; ++island)
        {
            for (size_t iteration = 0; iteration < COLLISION_SOLVER_ITERATIONS; ++iteration)
                for (size_t index : solverIslands[island])
                    resolveCollision(solverConstraints[index]);
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

bool PhysicsManager::preStepCollision(Collision *collision, float inverseTimeDelta, SolverConstraint &outConstraint)
{
    Rigidbody *rbA = (Rigidbody *)collision->getReferenceObject()->getGameObject()->getComponent(
        ComponentType::RIGID_BODY);
    Rigidbody *rbB =
        (Rigidbody *)collision->getIncidentObject()->getGameObject()->getComponent(ComponentType::RIGID_BODY);

    if (rbA == nullptr || rbB == nullptr)
    {
        return false;
    }

    const auto hasWakeMotion = [](const Rigidbody *other)
    {
        return other->getVelocity().squaredNorm() > LINEAR_SLEEP_THRESHOLD * LINEAR_SLEEP_THRESHOLD ||
               std::abs(other->getAngularVelocity()) > ANGULAR_SLEEP_THRESHOLD ||
               (other->getBodyType() == KINEMATIC &&
                (!other->getVelocity().isZero(0) || other->getAngularVelocity() != 0));
    };
    if (rbA->getIsSleeping() && hasWakeMotion(rbB))
    {
        rbA->wakeUp();
    }
    if (rbB->getIsSleeping() && hasWakeMotion(rbA))
    {
        rbB->wakeUp();
    }
    mergeSleepIslands(rbA, rbB);
    float invMassA = rbA->getIsSleeping() ? 0.f : rbA->getInverseMass();
    float invMassB = rbB->getIsSleeping() ? 0.f : rbB->getInverseMass();
    float invInertiaA = rbA->getIsSleeping() ? 0.f : rbA->getInverseMomentOfInertia();
    float invInertiaB = rbB->getIsSleeping() ? 0.f : rbB->getInverseMomentOfInertia();

    Eigen::Vector2f positionA = rbA->getGameObject()->worldPosition;
    Eigen::Vector2f positionB = rbB->getGameObject()->worldPosition;

    const Eigen::Vector2f &normal = collision->getNormal();
    const Eigen::Vector2f tangent = crossSV(1.0f, normal);
    float inverseMassSum = invMassA + invMassB;
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
        float rnA = rA.dot(normal);
        float rnB = rB.dot(normal);
        float kNormal =
            inverseMassSum + invInertiaA * (rA.dot(rA) - rnA * rnA) + invInertiaB * (rB.dot(rB) - rnB * rnB);
        contactPoints.points[i].massNormal = kNormal > 0 ? 1.0f / kNormal : 0.f;

        float rtA = rA.dot(tangent);
        float rtB = rB.dot(tangent);
        float kTangent =
            inverseMassSum + invInertiaA * (rA.dot(rA) - rtA * rtA) + invInertiaB * (rB.dot(rB) - rtB * rtB);
        contactPoints.points[i].massTangent = kTangent > 0 ? 1.0f / kTangent : 0.f;

        float baumgarteBias =
            -COLLISION_BIAS_FACTOR * inverseTimeDelta *
            std::min(0.0f, contactPoints.points[i].separation + COLLISION_ALLOWED_PENETRATION);

        Eigen::Vector2f relativeVelocity = (rbB->getVelocity() + crossSV(rbB->getAngularVelocity(), rB)) -
                                           (rbA->getVelocity() + crossSV(rbA->getAngularVelocity(), rA));
        float vn = relativeVelocity.dot(normal);
        float restitutionBias = 0.0f;
        if (vn < -RESTITUTION_VELOCITY_THRESHOLD)
        {
            restitutionBias = -collision->getRestitution() * vn;
        }

        contactPoints.points[i].bias = baumgarteBias + restitutionBias;

        Eigen::Vector2f accumulatedImpulse = contactPoints.points[i].accumulatedNormalImpulse * normal +
                                             contactPoints.points[i].accumulatedTangentImpulse * tangent;
        rbA->applySolverImpulse(-accumulatedImpulse, rA);
        rbB->applySolverImpulse(accumulatedImpulse, rB);
    }
    return true;
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

void PhysicsManager::updateSleepingStates(double timeDelta)
{
    PLATFORMATOR_WORK_ADD(sleepBodies, sleepCandidates.size());

    // Reduce all member motion and timers before applying any group decision.
    for (auto *body : sleepCandidates)
    {
        auto *root = findSleepRoot(body);
        root->islandQuiet &=
            body->getVelocity().squaredNorm() <= LINEAR_SLEEP_THRESHOLD * LINEAR_SLEEP_THRESHOLD &&
            std::abs(body->getAngularVelocity()) <= ANGULAR_SLEEP_THRESHOLD;
        root->islandTimer = std::min(root->islandTimer, body->getSleepTimer());
    }

    for (auto *body : sleepCandidates)
    {
        auto *root = findSleepRoot(body);
        const auto timer = root->islandQuiet ? root->islandTimer + timeDelta : 0;
        if (timer >= SLEEP_DELAY)
        {
            body->setIsSleeping(true);
        }
        else
        {
            body->setSleepTimer(timer);
        }
    }

    // Sleep decisions queue membership changes; drain only after both fixed-list traversals finish.
    reconcileChangedBodies();
}

void PhysicsManager::addPendingPhysicsEvent(const PhysicsEvent &event)
{
    pendingPhysicsEvents.push_back(event);
    PLATFORMATOR_WORK_ADD(contactEventRecords, 1);
    if (event.type == PhysicsEvent::COLLISION_ENTER)
    {
        PLATFORMATOR_BENCH_ADD_COUNTER(CollisionEnterEventCount, 1);
    }
    else
    {
        PLATFORMATOR_BENCH_ADD_COUNTER(CollisionExitEventCount, 1);
    }
}

const std::vector<PhysicsEvent> &PhysicsManager::getPendingPhysicsEvents() const
{
    return pendingPhysicsEvents;
}

void PhysicsManager::clearPendingPhysicsEvents()
{
    pendingPhysicsEvents.clear();
}

void PhysicsManager::handlePendingPhysicsEvents(double timeDelta)
{
    const auto key = [](const PhysicsEvent &event)
    {
        const int family = event.type == PhysicsEvent::COLLISION_EXIT ? 0 : 1;
        return std::tuple{family, event.colliderA->getHandle(), event.colliderB->getHandle()};
    };
    std::sort(pendingPhysicsEvents.begin(), pendingPhysicsEvents.end(), [&](const auto &a, const auto &b)
              { return key(a) < key(b); });
    std::exception_ptr error;
    for (const PhysicsEvent &event : pendingPhysicsEvents)
    {
        auto deliver = [&](Collider *receiver, Collider *other)
        {
            try
            {
                if (event.type == PhysicsEvent::COLLISION_ENTER)
                {
                    receiver->triggerCollisionEnter(event.collision, other, timeDelta);
                }
                else
                {
                    receiver->triggerCollisionExit(other, timeDelta);
                }
            }
            catch (...)
            {
                if (!error)
                {
                    error = std::current_exception();
                }
            }
        };
        deliver(event.colliderA, event.colliderB);
        deliver(event.colliderB, event.colliderA);
    }
    clearPendingPhysicsEvents();
    if (error)
    {
        std::rethrow_exception(error);
    }
}

const Eigen::Vector2f &PhysicsManager::getGravityVector() const
{
    return gravityVector;
}

const Eigen::Vector2f &PhysicsManager::getGravityVectorNormalized() const
{
    return gravityVectorNormalized;
}

PhysicsEvent PhysicsManager::makePhysicsEvent(PhysicsEvent::EventType type, const ColliderPair *pair,
                                              Collision *collision)
{
    return PhysicsEvent{type, collision, pair->getObjectA(), pair->getObjectB()};
}

namespace
{
    Eigen::Vector2d validateAndNormalizeRay(const Eigen::Vector2f &origin, const Eigen::Vector2f &direction,
                                            float maxDistance)
    {
        if (!origin.allFinite() || !direction.allFinite() || !std::isfinite(maxDistance) || maxDistance < 0.0f)
        {
            throw std::invalid_argument("Raycast origin, direction, and non-negative maxDistance must be finite");
        }
        const float maxAbs = direction.cwiseAbs().maxCoeff();
        if (maxAbs == 0.0f || (maxAbs <= 1e-6f && direction.squaredNorm() <= 1e-12f))
        {
            throw std::invalid_argument("Raycast direction must be non-zero");
        }
        const double scale = static_cast<double>(maxAbs);
        const Eigen::Vector2d scaled(static_cast<double>(direction.x()) / scale,
                                     static_cast<double>(direction.y()) / scale);
        return scaled / std::hypot(scaled.x(), scaled.y());
    }

    bool rayIntersectsAABBInterval(const Eigen::Vector2f &origin, const Eigen::Vector2d &dir, double dLimit,
                                   double minX, double maxX, double minY, double maxY, double tol,
                                   double &outTMin, double &outTMax) noexcept
    {
        const double bMinX = minX - tol;
        const double bMaxX = maxX + tol;
        const double bMinY = minY - tol;
        const double bMaxY = maxY + tol;
        const double ox = static_cast<double>(origin.x());
        const double oy = static_cast<double>(origin.y());
        const double dx = dir.x();
        const double dy = dir.y();

        double tMin = 0.0;
        double tMax = dLimit;
        if (dx == 0.0)
        {
            if (ox < bMinX || ox > bMaxX)
                return false;
        }
        else
        {
            double t1 = (bMinX - ox) / dx;
            double t2 = (bMaxX - ox) / dx;
            if (t1 > t2)
                std::swap(t1, t2);
            tMin = std::max(tMin, t1);
            tMax = std::min(tMax, t2);
            if (tMin > tMax)
                return false;
        }

        if (dy == 0.0)
        {
            if (oy < bMinY || oy > bMaxY)
                return false;
        }
        else
        {
            double t1 = (bMinY - oy) / dy;
            double t2 = (bMaxY - oy) / dy;
            if (t1 > t2)
                std::swap(t1, t2);
            tMin = std::max(tMin, t1);
            tMax = std::min(tMax, t2);
            if (tMin > tMax)
                return false;
        }

        if (tMin > dLimit || tMax < 0.0)
            return false;
        outTMin = tMin;
        outTMax = tMax;
        return true;
    }

    bool intersectColliderRay(Collider *collider, const Eigen::Vector2f &origin,
                              const Eigen::Vector2d &dir, float maxDistance, double limit, uint64_t collisionMask,
                              bool includeTriggers, float cMinX, float cMaxX, float cMinY, float cMaxY,
                              platformator::RaycastHit &outHit)
    {
        if (collider == nullptr || collider->isRetired())
            return false;
        GameObject *obj = collider->getGameObject();
        if (obj == nullptr || !obj->getActive() || obj->getIsMarkedForDeletion())
            return false;
        if ((collider->getCollisionGroup() & collisionMask) == 0ULL)
            return false;
        if (collider->getIsTrigger() && !includeTriggers)
            return false;

        const ColliderType shape = collider->getColliderType();
        float halfW = 0.0f;
        float halfH = 0.0f;
        float radius = 0.0f;
        double cosR = 1.0;
        double sinR = 0.0;
        if (shape == ColliderType::CircleCollider)
        {
            const auto *circle = static_cast<const CircleCollider *>(collider);
            radius = circle->getPublishedRadius();
            if (!std::isfinite(radius) || radius <= 0.0f)
                return false;
        }
        else if (shape == ColliderType::BoxCollider)
        {
            const auto *box = static_cast<const BoxCollider *>(collider);
            halfW = box->getPublishedHalfWidth();
            halfH = box->getPublishedHalfHeight();
            if (!std::isfinite(halfW) || !std::isfinite(halfH) || halfW <= 0.0f || halfH <= 0.0f)
                return false;
            const Eigen::Vector2f &xAxis = box->getPublishedXAxis();
            cosR = static_cast<double>(xAxis.x());
            sinR = static_cast<double>(xAxis.y());
        }
        else
        {
            return false;
        }

        const Eigen::Vector2f &center = collider->getCachedWorldPosition();
        if (!center.allFinite())
            return false;

        const double aabbMinX = static_cast<double>(cMinX);
        const double aabbMaxX = static_cast<double>(cMaxX);
        const double aabbMinY = static_cast<double>(cMinY);
        const double aabbMaxY = static_cast<double>(cMaxY);

        PLATFORMATOR_WORK_ADD(raycastCandidateVisits, 1);

        const double maxCoord =
            std::max({1.0, std::abs(aabbMinX), std::abs(aabbMaxX), std::abs(aabbMinY), std::abs(aabbMaxY)});
        const double aabbTol = std::max(1e-4, 1e-6 * maxCoord);
        const double dLimit = std::min(static_cast<double>(maxDistance), limit);
        double aabbTMin = 0.0;
        double aabbTMax = 0.0;
        if (!rayIntersectsAABBInterval(origin, dir, dLimit, aabbMinX, aabbMaxX, aabbMinY, aabbMaxY, aabbTol, aabbTMin,
                                       aabbTMax))
        {
            return false;
        }

        const double dirX = dir.x();
        const double dirY = dir.y();
        const double toCenterX = static_cast<double>(center.x()) - static_cast<double>(origin.x());
        const double toCenterY = static_cast<double>(center.y()) - static_cast<double>(origin.y());
        const double limitTol = 1e-8 * std::max(1.0, dLimit);

        float hitDistance = 0.0f;
        Eigen::Vector2f hitPoint = origin;
        Eigen::Vector2f hitNormal(static_cast<float>(-dirX), static_cast<float>(-dirY));

        if (shape == ColliderType::CircleCollider)
        {
            const double originDistSq = toCenterX * toCenterX + toCenterY * toCenterY;
            const double r = static_cast<double>(radius);
            const double rSq = r * r;
            if (originDistSq > rSq)
            {
                const double dirLenSq = dirX * dirX + dirY * dirY;
                const double proj = (toCenterX * dirX + toCenterY * dirY) / dirLenSq;
                if (proj < 0.0)
                    return false;
                const double perpX = toCenterX - proj * dirX;
                const double perpY = toCenterY - proj * dirY;
                const double perpSq = perpX * perpX + perpY * perpY;
                const double perpDist = std::sqrt(perpSq);
                const double tangencyTol = 1e-11 * std::max({1.0, r, std::abs(toCenterX), std::abs(toCenterY)});
                if (perpDist > r + tangencyTol)
                    return false;
                const double disc = std::max(0.0, rSq - perpSq);
                double tHit = std::max(0.0, proj - std::sqrt(disc));
                if (tHit > dLimit + limitTol)
                    return false;
                tHit = std::min(tHit, dLimit);
                const double exactHitX = static_cast<double>(origin.x()) + tHit * dirX;
                const double exactHitY = static_cast<double>(origin.y()) + tHit * dirY;
                hitDistance = static_cast<float>(tHit);
                hitPoint = Eigen::Vector2f(static_cast<float>(exactHitX), static_cast<float>(exactHitY));
                const double nx = exactHitX - static_cast<double>(center.x());
                const double ny = exactHitY - static_cast<double>(center.y());
                const double nLen = std::hypot(nx, ny);
                hitNormal = nLen > 0.0 ? Eigen::Vector2f(static_cast<float>(nx / nLen), static_cast<float>(ny / nLen))
                                       : Eigen::Vector2f(static_cast<float>(-dirX), static_cast<float>(-dirY));
            }
        }
        else
        {
            const double relX = -toCenterX;
            const double relY = -toCenterY;
            const double ox = relX * cosR + relY * sinR;
            const double oy = -relX * sinR + relY * cosR;
            const double dHalfW = static_cast<double>(halfW);
            const double dHalfH = static_cast<double>(halfH);

            if (std::abs(ox) > dHalfW || std::abs(oy) > dHalfH)
            {
                const double dx = dirX * cosR + dirY * sinR;
                const double dy = -dirX * sinR + dirY * cosR;
                double tMin = 0.0;
                double tMax = dLimit + limitTol;
                int hitAxis = -1;
                double hitSign = 0.0;

                const auto testSlab = [&](double o, double d, double halfExtent, int axis) -> bool
                {
                    if (d == 0.0)
                        return std::abs(o) <= halfExtent;
                    double t1 = (-halfExtent - o) / d;
                    double t2 = (halfExtent - o) / d;
                    double sign = -1.0;
                    if (t1 > t2)
                    {
                        std::swap(t1, t2);
                        sign = 1.0;
                    }
                    if (t1 > tMin)
                    {
                        tMin = t1;
                        hitAxis = axis;
                        hitSign = sign;
                    }
                    if (t2 < tMax)
                    {
                        tMax = t2;
                    }
                    return tMin <= tMax;
                };

                if (!testSlab(ox, dx, dHalfW, 0) || !testSlab(oy, dy, dHalfH, 1) || tMin > dLimit + limitTol ||
                    hitAxis < 0)
                    return false;

                tMin = std::min(tMin, dLimit);
                hitDistance = static_cast<float>(tMin);
                hitPoint = Eigen::Vector2f(static_cast<float>(static_cast<double>(origin.x()) + tMin * dirX),
                                           static_cast<float>(static_cast<double>(origin.y()) + tMin * dirY));
                const double lnx = (hitAxis == 0) ? hitSign : 0.0;
                const double lny = (hitAxis == 1) ? hitSign : 0.0;
                const double wnx = lnx * cosR - lny * sinR;
                const double wny = lnx * sinR + lny * cosR;
                const double nLen = std::hypot(wnx, wny);
                hitNormal = nLen > 0.0 ? Eigen::Vector2f(static_cast<float>(wnx / nLen), static_cast<float>(wny / nLen))
                                       : Eigen::Vector2f(static_cast<float>(-dirX), static_cast<float>(-dirY));
            }
        }

        outHit.collider = collider;
        outHit.gameObject = obj;
        outHit.point = hitPoint;
        outHit.normal = hitNormal;
        outHit.distance = hitDistance;
        outHit.fraction = maxDistance > 0.0f ? (hitDistance / maxDistance) : 0.0f;
        return true;
    }
} // namespace

template <typename HitCallback>
bool PhysicsManager::traverseRayCandidates(const Eigen::Vector2f &origin, const Eigen::Vector2d &dir, float maxDistance,
                                           const double &limit, uint64_t collisionMask, bool includeTriggers,
                                           HitCallback &&onHit) const
{
    platformator::RaycastHit candidate;
    const GridCellRange &activeBounds = grid.getActiveCellBounds();
    if (!activeBounds.getIsValid() || grid.getCells().empty())
        return false;

    const double gridMinX = static_cast<double>(activeBounds.minX) * static_cast<double>(GRID_CELL_SIZE);
    const double gridMaxX = static_cast<double>(activeBounds.maxX + 1) * static_cast<double>(GRID_CELL_SIZE);
    const double gridMinY = static_cast<double>(activeBounds.minY) * static_cast<double>(GRID_CELL_SIZE);
    const double gridMaxY = static_cast<double>(activeBounds.maxY + 1) * static_cast<double>(GRID_CELL_SIZE);

    double tGridStart = 0.0;
    double tGridEnd = 0.0;
    if (!rayIntersectsAABBInterval(origin, dir, limit, gridMinX, gridMaxX, gridMinY, gridMaxY,
                                   1.0, tGridStart, tGridEnd))
    {
        return true;
    }

    std::array<const Collider *, 64> seenMultiCell{};
    size_t seenMultiCellCount = 0;

    const double ox = static_cast<double>(origin.x());
    const double oy = static_cast<double>(origin.y());
    const double dx = dir.x();
    const double dy = dir.y();

    const auto visitCellInterval = [&](const GridCell &cell, double tEnter, double tExit)
    {
        const double tClampedEnter = std::max(0.0, tEnter);
        const double tClampedExit = std::max(tClampedEnter, std::min(limit, tExit));
        const double p0x = ox + tClampedEnter * dx;
        const double p0y = oy + tClampedEnter * dy;
        const double p1x = ox + tClampedExit * dx;
        const double p1y = oy + tClampedExit * dy;
        const float maxAbsCoord = static_cast<float>(
            std::max({1.0, std::abs(p0x), std::abs(p1x), std::abs(p0y), std::abs(p1y)}));
        const float pad = 1e-2f + 1e-5f * maxAbsCoord;
        const float segMinX = static_cast<float>(std::min(p0x, p1x)) - pad;
        const float segMaxX = static_cast<float>(std::max(p0x, p1x)) + pad;
        const float segMinY = static_cast<float>(std::min(p0y, p1y)) - pad;
        const float segMaxY = static_cast<float>(std::max(p0y, p1y)) + pad;

        cell.getAABB()->forEachOverlappingAABB(
            segMinX, segMaxX, segMinY, segMaxY,
            [&](Collider *c, float cMinX, float cMaxX, float cMinY, float cMaxY)
            {
                const GridCellRange &range = c->getGridCellRange();
                if (range.minX != range.maxX || range.minY != range.maxY)
                {
                    for (size_t i = 0; i < seenMultiCellCount; ++i)
                    {
                        if (seenMultiCell[i] == c)
                            return;
                    }
                    if (seenMultiCellCount < seenMultiCell.size())
                        seenMultiCell[seenMultiCellCount++] = c;
                }
                if (intersectColliderRay(c, origin, dir, maxDistance, limit, collisionMask, includeTriggers,
                                         cMinX, cMaxX, cMinY, cMaxY, candidate))
                {
                    onHit(candidate);
                }
            });
    };

    const double tStart = std::max(0.0, tGridStart);
    const double tEnd = std::min(limit, tGridEnd);
    if (tStart > tEnd)
        return true;

    const double cellSize = static_cast<double>(GRID_CELL_SIZE);
    const double startX = ox + tStart * dx;
    const double startY = oy + tStart * dy;
    const double endX = ox + tEnd * dx;
    const double endY = oy + tEnd * dy;

    const int startCx = static_cast<int>(std::floor(startX / cellSize));
    const int startCy = static_cast<int>(std::floor(startY / cellSize));
    const int endCx = static_cast<int>(std::floor(endX / cellSize));
    const int endCy = static_cast<int>(std::floor(endY / cellSize));
    const std::int64_t maxSteps =
        std::abs(static_cast<std::int64_t>(endCx) - startCx) + std::abs(static_cast<std::int64_t>(endCy) - startCy) + 1;

    constexpr std::int64_t kMaxGridDdaSteps = 65536;
    if (maxSteps <= kMaxGridDdaSteps)
    {
        const int stepX = (dx > 0.0) ? 1 : (dx < 0.0 ? -1 : 0);
        const int stepY = (dy > 0.0) ? 1 : (dy < 0.0 ? -1 : 0);
        const double inf = std::numeric_limits<double>::infinity();
        double tDeltaX = inf;
        double tMaxX = inf;
        if (stepX != 0)
        {
            tDeltaX = cellSize / std::abs(dx);
            const double nextX = static_cast<double>(stepX > 0 ? (startCx + 1) : startCx) * cellSize;
            tMaxX = std::max(tStart, (nextX - ox) / dx);
        }
        double tDeltaY = inf;
        double tMaxY = inf;
        if (stepY != 0)
        {
            tDeltaY = cellSize / std::abs(dy);
            const double nextY = static_cast<double>(stepY > 0 ? (startCy + 1) : startCy) * cellSize;
            tMaxY = std::max(tStart, (nextY - oy) / dy);
        }

        const double maxAbsCoord =
            std::max({1.0, std::abs(startX), std::abs(startY), std::abs(endX), std::abs(endY)});
        const double boundaryCoordEps = std::max(1e-4, 1e-6 * maxAbsCoord);
        const double tCornerEps = 1e-7 * std::max(1.0, tEnd) + 1e-12;

        std::array<GridCellKey, 16> recentCells{
            GridCellKey(0, 0), GridCellKey(0, 0), GridCellKey(0, 0), GridCellKey(0, 0),
            GridCellKey(0, 0), GridCellKey(0, 0), GridCellKey(0, 0), GridCellKey(0, 0),
            GridCellKey(0, 0), GridCellKey(0, 0), GridCellKey(0, 0), GridCellKey(0, 0),
            GridCellKey(0, 0), GridCellKey(0, 0), GridCellKey(0, 0), GridCellKey(0, 0)};
        size_t recentCount = 0;
        size_t recentWrite = 0;
        const auto visitOnce = [&](int vx, int vy, double t0, double t1)
        {
            const GridCellKey key(vx, vy);
            for (size_t i = 0; i < recentCount; ++i)
            {
                if (recentCells[i] == key)
                    return;
            }
            recentCells[recentWrite] = key;
            recentWrite = (recentWrite + 1) % recentCells.size();
            if (recentCount < recentCells.size())
                ++recentCount;
            if (const GridCell *cell = grid.findCell(key))
                visitCellInterval(*cell, t0, t1);
        };

        const int sMinCx = static_cast<int>(std::floor((startX - boundaryCoordEps) / cellSize));
        const int sMaxCx = static_cast<int>(std::floor((startX + boundaryCoordEps) / cellSize));
        const int sMinCy = static_cast<int>(std::floor((startY - boundaryCoordEps) / cellSize));
        const int sMaxCy = static_cast<int>(std::floor((startY + boundaryCoordEps) / cellSize));
        for (int bx = sMinCx; bx <= sMaxCx; ++bx)
        {
            for (int by = sMinCy; by <= sMaxCy; ++by)
            {
                const bool behindX = stepX != 0 && (bx - startCx) * stepX < 0;
                const bool behindY = stepY != 0 && (by - startCy) * stepY < 0;
                if (behindX || behindY)
                    visitOnce(bx, by, tStart, tStart);
            }
        }
        const int parallelAltCx = (stepX == 0 && sMinCx != sMaxCx) ? ((sMinCx == startCx) ? sMaxCx : sMinCx) : 0;
        const bool hasParallelAltX = (stepX == 0 && sMinCx != sMaxCx);
        const int parallelAltCy = (stepY == 0 && sMinCy != sMaxCy) ? ((sMinCy == startCy) ? sMaxCy : sMinCy) : 0;
        const bool hasParallelAltY = (stepY == 0 && sMinCy != sMaxCy);

        int cx = startCx;
        int cy = startCy;
        double tCellEnter = tStart;
        const std::int64_t stepBudget = maxSteps + 4;
        for (std::int64_t step = 0; step <= stepBudget; ++step)
        {
            const double activeEnd = std::clamp(std::min(limit, tEnd), tStart, tEnd);
            const double tLimitEnd = activeEnd + tCornerEps;
            if (tCellEnter > tLimitEnd)
                break;

            const double t0 = std::clamp(tCellEnter, tStart, activeEnd);
            const double t1 = std::clamp(std::min({activeEnd, tMaxX, tMaxY}), t0, activeEnd);
            visitOnce(cx, cy, t0, t1);
            if (hasParallelAltX)
                visitOnce(parallelAltCx, cy, t0, t1);
            if (hasParallelAltY)
                visitOnce(cx, parallelAltCy, t0, t1);
            if (hasParallelAltX && hasParallelAltY)
                visitOnce(parallelAltCx, parallelAltCy, t0, t1);

            const double postHitEnd = std::clamp(std::min(limit, tEnd), tStart, tEnd);
            const double postHitLimit = postHitEnd + tCornerEps;
            if (cx == endCx && cy == endCy && tMaxX > postHitLimit && tMaxY > postHitLimit)
                break;

            if (std::isfinite(tMaxX) && std::isfinite(tMaxY) && std::abs(tMaxX - tMaxY) <= tCornerEps)
            {
                if (std::min(tMaxX, tMaxY) > postHitLimit)
                    break;
                const double tC0 = std::clamp(std::min(tMaxX, tMaxY), tStart, postHitEnd);
                const double tC1 = std::clamp(std::max(tMaxX, tMaxY), tC0, postHitEnd);
                visitOnce(cx + stepX, cy, tC0, tC1);
                visitOnce(cx, cy + stepY, tC0, tC1);
                tCellEnter = std::max(tMaxX, tMaxY);
                tMaxX += tDeltaX;
                tMaxY += tDeltaY;
                cx += stepX;
                cy += stepY;
            }
            else if (tMaxX < tMaxY)
            {
                if (!std::isfinite(tMaxX) || tMaxX > postHitLimit)
                    break;
                tCellEnter = tMaxX;
                tMaxX += tDeltaX;
                cx += stepX;
            }
            else
            {
                if (!std::isfinite(tMaxY) || tMaxY > postHitLimit)
                    break;
                tCellEnter = tMaxY;
                tMaxY += tDeltaY;
                cy += stepY;
            }
        }

        const double finalEnd = std::clamp(std::min(limit, tEnd), tStart, tEnd);
        const double finalX = ox + finalEnd * dx;
        const double finalY = oy + finalEnd * dy;
        const int eMinCx = static_cast<int>(std::floor((finalX - boundaryCoordEps) / cellSize));
        const int eMaxCx = static_cast<int>(std::floor((finalX + boundaryCoordEps) / cellSize));
        const int eMinCy = static_cast<int>(std::floor((finalY - boundaryCoordEps) / cellSize));
        const int eMaxCy = static_cast<int>(std::floor((finalY + boundaryCoordEps) / cellSize));
        for (int ex = eMinCx; ex <= eMaxCx; ++ex)
        {
            for (int ey = eMinCy; ey <= eMaxCy; ++ey)
            {
                visitOnce(ex, ey, finalEnd, finalEnd);
            }
        }
        return true;
    }

    for (const auto &entry : grid.getCells())
    {
        const GridCellKey &key = entry.first;
        const double cMinX = static_cast<double>(key.x) * cellSize;
        const double cMaxX = static_cast<double>(key.x + 1) * cellSize;
        const double cMinY = static_cast<double>(key.y) * cellSize;
        const double cMaxY = static_cast<double>(key.y + 1) * cellSize;
        double cTEnter = 0.0;
        double cTExit = 0.0;
        if (rayIntersectsAABBInterval(origin, dir, limit, cMinX, cMaxX, cMinY, cMaxY, 1e-2,
                                      cTEnter, cTExit))
        {
            visitCellInterval(entry.second, cTEnter, cTExit);
        }
    }
    return true;
}

std::optional<platformator::RaycastHit> PhysicsManager::raycast(const Eigen::Vector2f &origin,
                                                                const Eigen::Vector2f &direction, float maxDistance,
                                                                uint64_t collisionMask, bool includeTriggers) const
{
    platformator::RaycastHit hit;
    if (!raycast(origin, direction, maxDistance, hit, collisionMask, includeTriggers))
        return std::nullopt;
    return hit;
}

bool PhysicsManager::raycast(const Eigen::Vector2f &origin, const Eigen::Vector2f &direction, float maxDistance,
                             platformator::RaycastHit &outHit, uint64_t collisionMask, bool includeTriggers) const
{
    const Eigen::Vector2d dir = validateAndNormalizeRay(origin, direction, maxDistance);

    bool hasHit = false;
    double limit = static_cast<double>(maxDistance);
    platformator::RaycastHit best;

    traverseRayCandidates(origin, dir, maxDistance, limit, collisionMask, includeTriggers,
                          [&](const platformator::RaycastHit &candidate)
                          {
                              if (!hasHit || candidate.distance < best.distance ||
                                  (candidate.distance == best.distance &&
                                   candidate.collider->getId() < best.collider->getId()))
                              {
                                  best = candidate;
                                  hasHit = true;
                                  // Keep the upper bound of best.distance's float rounding interval so
                                  // equal-float-distance candidates with lower IDs are never pruned early.
                                  const double nextAfterMid =
                                      0.5 * (static_cast<double>(best.distance) +
                                             static_cast<double>(std::nextafter(
                                                 best.distance, std::numeric_limits<float>::infinity())));
                                  limit = std::min(static_cast<double>(maxDistance), nextAfterMid);
                              }
                          });

    if (hasHit)
        outHit = best;
    return hasHit;
}

std::vector<platformator::RaycastHit> PhysicsManager::raycastAll(const Eigen::Vector2f &origin,
                                                                 const Eigen::Vector2f &direction, float maxDistance,
                                                                 uint64_t collisionMask, bool includeTriggers) const
{
    const Eigen::Vector2d dir = validateAndNormalizeRay(origin, direction, maxDistance);

    std::vector<platformator::RaycastHit> hits;
    const double limit = static_cast<double>(maxDistance);
    const bool usedGrid = traverseRayCandidates(
        origin, dir, maxDistance, limit, collisionMask, includeTriggers,
        [&](const platformator::RaycastHit &candidate)
        { hits.push_back(candidate); });

    if (usedGrid && hits.size() > 1)
    {
        std::sort(hits.begin(), hits.end(), [](const platformator::RaycastHit &a, const platformator::RaycastHit &b)
                  {
            if (a.collider != b.collider)
                return a.collider < b.collider;
            return a.distance < b.distance; });
        hits.erase(std::unique(hits.begin(), hits.end(),
                               [](const platformator::RaycastHit &a, const platformator::RaycastHit &b)
                               {
                                   return a.collider == b.collider;
                               }),
                   hits.end());
    }

    std::sort(hits.begin(), hits.end(), [](const platformator::RaycastHit &a, const platformator::RaycastHit &b)
              {
        if (a.distance != b.distance)
            return a.distance < b.distance;
        return a.collider->getId() < b.collider->getId(); });
    return hits;
}
