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
    tbb::parallel_for(size_t(0), awakeDynamicBodies.size(), [&](size_t i)
                      {
        auto *body = awakeDynamicBodies[i];
        resetSleepIsland(body);
        body->integrateForces(timeDelta, gravityVector); });
}

void PhysicsManager::applyMovement(double timeDelta)
{
    PLATFORMATOR_WORK_ADD(movingBodies, awakeDynamicBodies.size() + movingKinematicBodies.size());
    if (awakeDynamicBodies.empty() && movingKinematicBodies.empty())
    {
        return;
    }
    tbb::parallel_for(size_t(0), awakeDynamicBodies.size() + movingKinematicBodies.size(), [&](size_t i)
                      {
        auto *body = i < awakeDynamicBodies.size() ? awakeDynamicBodies[i]
                                                   : movingKinematicBodies[i - awakeDynamicBodies.size()];
        if (const auto changes = body->integrateLocal(timeDelta))
        {
            movedObjects.local().push_back({body->getGameObject(), (changes & 1) != 0, (changes & 2) != 0});
        } });
    auto &manager = platformator_detail::RuntimeAccess::gameManager();
    for (auto &objects : movedObjects)
    {
        for (auto changed : objects)
        {
            if (changed.rotated)
            {
                manager.publishRenderGeometryChange(changed.object);
            }
            if (changed.translated)
            {
                // The manager synchronizes this batch before any callbacks or reads.
                manager.queueTransform(changed.object, false);
            }
            else
            {
                changed.object->updateCollider();
            }
        }
        objects.clear();
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
    grid.flushPendingCellUpdates();
    flushSupportDeltas();
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
        if (pair->resultHadContact && !pair->getCollision())
        {

            addPendingPhysicsEvent(makePhysicsEvent(PhysicsEvent::COLLISION_EXIT, pair, nullptr));
        }

        if (!pair->resultHadContact && pair->getCollision())
        {
            addPendingPhysicsEvent(
                makePhysicsEvent(PhysicsEvent::COLLISION_ENTER, pair, pair->getCollision()));
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
    dynamicCollisionScratch.clear();

    for (auto *pair : solverPairs)
    {
        auto *collision = pair->getCollision();
        const Collider *referenceCollider = collision->getReferenceObject();
        const Collider *incidentCollider = collision->getIncidentObject();

        Collision::CollisionType resolutionType = collision->getResolutionType();

        switch (resolutionType)
        {
        case Collision::CollisionType::DYNAMIC:
            preStepCollision(collision, inverseTimeDelta);
            dynamicCollisionScratch.push_back(collision);
            break;
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

    for (int iteration = 0; iteration < COLLISION_SOLVER_ITERATIONS; iteration++)
    {
        for (Collision *collision : dynamicCollisionScratch)
        {
            resolveCollision(collision);
        }
    }

    reconcileChangedBodies();
    updateSleepingStates(timeDelta);

#if PLATFORMATOR_ENABLE_BENCHMARKS
    PLATFORMATOR_BENCH_SET_COUNTER(AwakeDynamicBodyCount, countAwakeDynamicBodies());
#endif
}

void PhysicsManager::preStepCollision(Collision *collision, float inverseTimeDelta)
{
    Rigidbody *rbA = (Rigidbody *)collision->getReferenceObject()->getGameObject()->getComponent(
        ComponentType::RIGID_BODY);
    Rigidbody *rbB =
        (Rigidbody *)collision->getIncidentObject()->getGameObject()->getComponent(ComponentType::RIGID_BODY);

    if (rbA == nullptr || rbB == nullptr)
    {
        return;
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
    Eigen::Vector2f tangent = crossSV(1.0f, normal);
    float inverseMassSum = invMassA + invMassB;
    ClipPointsWithData &contactPoints = collision->getContactPoints();
    collision->solverFriction = collision->getFriction();
    collision->blockSolve = false;
    if (contactPoints.count == 2)
    {
        const auto rA0 = (contactPoints.points[0].point.point - positionA).eval();
        const auto rA1 = (contactPoints.points[1].point.point - positionA).eval();
        const auto rB0 = (contactPoints.points[0].point.point - positionB).eval();
        const auto rB1 = (contactPoints.points[1].point.point - positionB).eval();
        const float a0 = cross2D(rA0, normal), a1 = cross2D(rA1, normal);
        const float b0 = cross2D(rB0, normal), b1 = cross2D(rB1, normal);
        const float k00 = inverseMassSum + invInertiaA * a0 * a0 + invInertiaB * b0 * b0;
        const float k11 = inverseMassSum + invInertiaA * a1 * a1 + invInertiaB * b1 * b1;
        const float k01 = inverseMassSum + invInertiaA * a0 * a1 + invInertiaB * b0 * b1;
        const float determinant = k00 * k11 - k01 * k01;
        if (determinant > 0 && k00 * k00 < 1000.f * determinant)
        {
            collision->normalK << k00, k01, k01, k11;
            collision->normalMass << k11, -k01, -k01, k00;
            collision->normalMass /= determinant;
            collision->blockSolve = true;
        }
    }

    for (size_t i = 0; i < contactPoints.count; i++)
    {
        Eigen::Vector2f rA = contactPoints.points[i].point.point - positionA;
        Eigen::Vector2f rB = contactPoints.points[i].point.point - positionB;
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
}

void PhysicsManager::resolveCollision(const Collision *collision)
{
    GameObject *gameObjectA = collision->getReferenceObject()->getGameObject();
    GameObject *gameObjectB = collision->getIncidentObject()->getGameObject();

    Rigidbody *rbA = (Rigidbody *)gameObjectA->getComponent(ComponentType::RIGID_BODY);
    Rigidbody *rbB = (Rigidbody *)gameObjectB->getComponent(ComponentType::RIGID_BODY);

    if (rbA == nullptr || rbB == nullptr)
    {
        return;
    }

    Eigen::Vector2f positionA = gameObjectA->worldPosition;
    Eigen::Vector2f positionB = gameObjectB->worldPosition;

    float invMassA = rbA->getIsSleeping() ? 0.f : rbA->getInverseMass();
    float invMassB = rbB->getIsSleeping() ? 0.f : rbB->getInverseMass();

    float invInertiaA = rbA->getIsSleeping() ? 0.f : rbA->getInverseMomentOfInertia();
    float invInertiaB = rbB->getIsSleeping() ? 0.f : rbB->getInverseMomentOfInertia();
    const Eigen::Vector2f &normal = collision->getNormal();
    Eigen::Vector2f tangent = crossSV(1.0f, normal);
    ClipPointsWithData &contactPoints = collision->getContactPoints();
    if (collision->blockSolve)
    {
        const Eigen::Vector2f rA[] = {(contactPoints.points[0].point.point - positionA).eval(),
                                      (contactPoints.points[1].point.point - positionA).eval()};
        const Eigen::Vector2f rB[] = {(contactPoints.points[0].point.point - positionB).eval(),
                                      (contactPoints.points[1].point.point - positionB).eval()};
        const Eigen::Vector2f previous(contactPoints.points[0].accumulatedNormalImpulse,
                                       contactPoints.points[1].accumulatedNormalImpulse);
        Eigen::Vector2f rhs;
        for (int i = 0; i < 2; ++i)
        {
            rhs[i] = ((rbB->getVelocity() + crossSV(rbB->getAngularVelocity(), rB[i])) -
                      (rbA->getVelocity() + crossSV(rbA->getAngularVelocity(), rA[i])))
                         .dot(normal) -
                     contactPoints.points[i].bias;
        }
        rhs -= collision->normalK * previous;
        Eigen::Vector2f next = -collision->normalMass * rhs;
        bool accepted = next.x() >= 0 && next.y() >= 0;
        if (!accepted)
        {
            next = {-rhs.x() / collision->normalK(0, 0), 0};
            accepted = next.x() >= 0 && collision->normalK(1, 0) * next.x() + rhs.y() >= 0;
        }
        if (!accepted)
        {
            next = {0, -rhs.y() / collision->normalK(1, 1)};
            accepted = next.y() >= 0 && collision->normalK(0, 1) * next.y() + rhs.x() >= 0;
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
                rbA->applySolverImpulse(-impulse, rA[i]);
                rbB->applySolverImpulse(impulse, rB[i]);
                contactPoints.points[i].accumulatedNormalImpulse = next[i];
            }
        }
    }

    // TODO: paralelize this
    // or not
    for (size_t i = 0; i < contactPoints.count; i++)
    {
        Eigen::Vector2f rA = contactPoints.points[i].point.point - positionA;
        Eigen::Vector2f rB = contactPoints.points[i].point.point - positionB;

        Eigen::Vector2f relativeVelocity = (rbB->getVelocity() + crossSV(rbB->getAngularVelocity(), rB)) -
                                           (rbA->getVelocity() + crossSV(rbA->getAngularVelocity(), rA));

        if (!collision->blockSolve)
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
            collision->solverFriction * contactPoints.points[i].accumulatedNormalImpulse;
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
        gameObject->queueTransform();
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
