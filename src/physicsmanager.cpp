#include "physicsmanager.h"
#include "physicsgeometry.h"

#include "benchmark.h"
#include "constants.h"
#include "debugdraw.h"
#include "gamemanager.h"
#include "oneapi/tbb.h"
#include "platformator/boxcollider.h"
#include "runtimeaccess.h"
#include "workprobe.h"

#include <exception>
#include <limits>
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
    tbb::parallel_for(size_t(0), awakeDynamicBodies.size(), [&](size_t i) {
        auto *body = awakeDynamicBodies[i];
        resetSleepIsland(body);
        body->integrateForces(timeDelta, gravityVector);
    });
}

void PhysicsManager::applyMovement(double timeDelta)
{
    PLATFORMATOR_WORK_ADD(movingBodies, awakeDynamicBodies.size() + movingKinematicBodies.size());
    if (awakeDynamicBodies.empty() && movingKinematicBodies.empty())
    {
        return;
    }
    tbb::parallel_for(size_t(0), awakeDynamicBodies.size() + movingKinematicBodies.size(), [&](size_t i) {
        auto *body = i < awakeDynamicBodies.size() ? awakeDynamicBodies[i]
                                                   : movingKinematicBodies[i - awakeDynamicBodies.size()];
        if (const auto changes = body->integrateLocal(timeDelta))
        {
            movedObjects.local().push_back({body->getGameObject(), (changes & 1) != 0, (changes & 2) != 0});
        }
    });
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
        tbb::parallel_for(size_t(0), pendingPairs.size(), [&](size_t i) {
            processNarrowPhasePair(pendingPairs[i]);
        });
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

    const auto hasWakeMotion = [](const Rigidbody *other) {
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
    const auto key = [](const PhysicsEvent &event) {
        const int family = event.type == PhysicsEvent::COLLISION_EXIT ? 0 : 1;
        return std::tuple{family, event.colliderA->getHandle(), event.colliderB->getHandle()};
    };
    std::sort(pendingPhysicsEvents.begin(), pendingPhysicsEvents.end(), [&](const auto &a, const auto &b) {
        return key(a) < key(b);
    });
    std::exception_ptr error;
    for (const PhysicsEvent &event : pendingPhysicsEvents)
    {
        auto deliver = [&](Collider *receiver, Collider *other) {
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
