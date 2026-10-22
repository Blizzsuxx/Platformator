#include "physicsmanager.h"

#include "platformator/boxcollider.h"
#include "benchmark.h"
#include "constants.h"
#include "debugdraw.h"
#include "gamemanager.h"
#include "runtimeaccess.h"
#include "workprobe.h"
#include "oneapi/tbb.h"

#include <limits>
#include <exception>

PhysicsManager::PhysicsManager()
    : rigidBodyComponents(), colliderComponents(), activeCollisions(), pendingPhysicsEvents(), grid(this),
      gravityVector(GRAVITY_VECTOR_X, GRAVITY_VECTOR_Y), gravityVectorNormalized(gravityVector.normalized())
{
}

PhysicsManager::~PhysicsManager()
{
}

void PhysicsManager::applyPhysics(double timeDelta)
{
    drainBodyChanges();
    PLATFORMATOR_WORK_ADD(forceBodies, awakeBodies.size());
    if (awakeBodies.empty())
        return;
    tbb::parallel_for(size_t(0), awakeBodies.size(), [&](size_t i) {
        auto *body = awakeBodies[i];
        body->sleepParent = body;
        body->sleepRank = 0;
        body->islandQuiet = true;
        body->islandTimer = body->getSleepTimer();
        body->integrateForces(timeDelta, gravityVector);
    });
}

void PhysicsManager::applyMovement(double timeDelta)
{
    PLATFORMATOR_WORK_ADD(movingBodies, awakeBodies.size() + kinematicBodies.size());
    if (awakeBodies.empty() && kinematicBodies.empty())
        return;
    tbb::parallel_for(size_t(0), awakeBodies.size() + kinematicBodies.size(), [&](size_t i) {
        auto *body = i < awakeBodies.size() ? awakeBodies[i] : kinematicBodies[i - awakeBodies.size()];
        if (const auto changes = body->integrateLocal(timeDelta))
            movedObjects.local().push_back({body->getGameObject(), (changes & 1) != 0});
    });
    auto &manager = platformator_detail::RuntimeAccess::gameManager();
    for (auto &objects : movedObjects)
    {
        for (auto changed : objects)
        {
            if (changed.translated)
                manager.queueTransform(changed.object);
            else
                changed.object->updateCollider();
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

    rigidBodyComponent->setPhysicsManagerIndex(rigidBodyComponents.size());
    rigidBodyComponent->setIsRegisteredInPhysicsManager(true);
    rigidBodyComponents.push_back(rigidBodyComponent);
    queueBodyChange(rigidBodyComponent);
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

// void PhysicsManager::refreshColliderComponent(Collider *colliderComponent)
// {
//     if (!colliderComponent->getGameObject()->getActive() || !colliderComponent->getIsRegisteredInGrid())
//     {
//         return;
//     }

//     grid.removeCollider(colliderComponent);
//     colliderComponent->setIsRegisteredInGrid(false);
//     grid.addCollider(colliderComponent);
//     colliderComponent->setIsRegisteredInGrid(true);
// }

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
    size_t lastIndex = rigidBodyComponents.size() - 1;

    if (removeIndex != lastIndex)
    {
        Rigidbody *movedRigidBody = rigidBodyComponents[lastIndex];
        rigidBodyComponents[removeIndex] = movedRigidBody;
        movedRigidBody->setPhysicsManagerIndex(removeIndex);
    }

    rigidBodyComponents.pop_back();
    rigidBodyComponent->setPhysicsManagerIndex(SIZE_MAX);
    rigidBodyComponent->setIsRegisteredInPhysicsManager(false);
    cancelBodyChange(rigidBodyComponent);
    reconcileBody(rigidBodyComponent);
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

    ++simulationStep;
    const auto &pendingPairs = *grid.getPendingNarrowPhasePairs();
    if (pendingPairs.size() < NARROW_PHASE_PARALLEL_THRESHOLD)
        for (auto *pair : pendingPairs)
            processNarrowPhasePair(pair);
    else
        tbb::parallel_for(size_t(0), pendingPairs.size(), [&](size_t i) { processNarrowPhasePair(pendingPairs[i]); });
    for (auto *pair : pendingPairs)
    {
        PLATFORMATOR_WORK_ADD(geometryPairs, pair->resultTestedGeometry ? 1 : 0);
        if (pair->resultHadContact && !pair->getCollision())
            pair->queueCollisionExit();
        if (!pair->resultHadContact && pair->getCollision())
            pair->enteredStep = simulationStep;
        reducePairSupport(pair);
        refreshPairLists(pair);
    }
    flushSupportDeltas();
    canonicalizePairs();
    const auto eventKey = [](const PhysicsEvent &event) {
        auto a = event.colliderA->getHandle(), b = event.colliderB->getHandle();
        if (b < a)
            std::swap(a, b);
        return std::pair{a, b};
    };
    // Only changed exits need sorting; persistent subscribed contacts already have canonical order.
    std::sort(pendingPhysicsEvents.begin(), pendingPhysicsEvents.end(),
              [&](const auto &a, const auto &b) { return eventKey(a) < eventKey(b); });
    PLATFORMATOR_WORK_ADD(stayPairs, stayPairs.size());
    for (auto *pair : stayPairs)
        pendingPhysicsEvents.push_back(makePhysicsEvent(
            pair->enteredStep == simulationStep ? PhysicsEvent::COLLISION_ENTER : PhysicsEvent::COLLISION_STAY, pair,
            pair->getCollision()));

#if PLATFORMATOR_ENABLE_DEBUG_TOOLS
    for (Collision *collision : activeCollisions)
    {
        DebugDraw::getInstance().addCollisionDebugObject(*collision);
    }
#endif

#if PLATFORMATOR_ENABLE_BENCHMARKS
    PLATFORMATOR_BENCH_SET_COUNTER(PendingNarrowPhasePairCount, pendingPairs.size());
    PLATFORMATOR_BENCH_SET_COUNTER(ActiveCollisionCount, contactPairs.size());

    int64_t enterCount = 0;
    int64_t stayCount = 0;
    int64_t exitCount = 0;

    for (const PhysicsEvent &event : pendingPhysicsEvents)
    {
        switch (event.type)
        {
        case PhysicsEvent::COLLISION_ENTER:
            enterCount++;
            break;
        case PhysicsEvent::COLLISION_STAY:
            stayCount++;
            break;
        case PhysicsEvent::COLLISION_EXIT:
            exitCount++;
            break;
        }
    }

    PLATFORMATOR_BENCH_SET_COUNTER(CollisionEnterEventCount, enterCount);
    PLATFORMATOR_BENCH_SET_COUNTER(CollisionStayEventCount, stayCount);
    PLATFORMATOR_BENCH_SET_COUNTER(CollisionExitEventCount, exitCount);
#endif

    grid.clearPendingNarrowPhasePairs();
}

#if PLATFORMATOR_ENABLE_BENCHMARKS
size_t PhysicsManager::countAwakeDynamicBodies() const
{
    return awakeBodies.size();
}
#endif

void PhysicsManager::processNarrowPhasePair(const ColliderPair *pair)
{
    pair->setIsQueuedForNarrowPhase(false);
    pair->setNarrowPhaseQueueIndex(SIZE_MAX);
    pair->resultHadContact = pair->getCollision() != nullptr;
    pair->resultTestedGeometry = false;
    if (!pair->getObjectA()->getGameObject()->getActive() || !pair->getObjectB()->getGameObject()->getActive() ||
        pair->getObjectA()->isRetired() || pair->getObjectB()->isRetired())
    {
        pair->clearCollisionNoEvent();
        return;
    }
    if (pair->shouldUpdate())
    {
        pair->resultTestedGeometry = true;
        satCreateCollision(*pair);
    }
}

bool PhysicsManager::checkProjections(std::span<const Eigen::Vector2f> normals, const Collider *referenceCollider,
                                      const Collider *incidentCollider, float &minOverlap, Eigen::Vector2f &minNormal,
                                      const Collider *&realIncidentCollider)
{
    for (long unsigned int i = 0; i < normals.size(); i++)
    {
        const Eigen::Vector2f &normal = normals[i];
        Eigen::Vector2f projections1 = referenceCollider->projectOntoAxis(
            normal); // we are using the index on the owner of the normal to further optimize projection
        Eigen::Vector2f projections2 = incidentCollider->projectOntoAxis(
            normal); // TODO: see if it's better to approximate from the perspective of the incident or reference object

        if (projections1.y() < projections2.x() || projections2.y() < projections1.x())
        {
            return false;
        }
        else
        {
            float overlap = std::min(projections1.y() - projections2.x(), projections2.y() - projections1.x());
            if (overlap < minOverlap &&
                (realIncidentCollider == incidentCollider || overlap < minOverlap * 0.98f - 0.001f))
            {
                minOverlap = overlap;
                minNormal = normal;
                realIncidentCollider = incidentCollider;
            }
        }
    }

    return true;
}

void PhysicsManager::satCreateCollision(const ColliderPair &pair)
{
    const Collider *referenceCollider = pair.getObjectA();
    const Collider *incidentCollider = pair.getObjectB();
    if (referenceCollider->getColliderType() == ColliderType::CircleCollider &&
        incidentCollider->getColliderType() == ColliderType::CircleCollider)
    {
        circleCircleCollision(pair);
        return;
    }
    if (referenceCollider->getColliderType() == ColliderType::CircleCollider ||
        incidentCollider->getColliderType() == ColliderType::CircleCollider)
    {
        circleBoxCollision(pair);
        return;
    }

    float minOverlap = std::numeric_limits<float>::max();
    Eigen::Vector2f minNormal;
    const Collider *realIncidentCollider = incidentCollider;

    std::span<const Eigen::Vector2f> normals = static_cast<const BoxCollider *>(referenceCollider)->getAxes();
    if (checkProjections(normals, referenceCollider, incidentCollider, minOverlap, minNormal, realIncidentCollider) ==
        false)
    {
        pair.clearCollisionNoEvent();
        return;
    }

    normals = static_cast<const BoxCollider *>(incidentCollider)->getAxes();

    if (checkProjections(normals, incidentCollider, referenceCollider, minOverlap, minNormal, realIncidentCollider) ==
        false)
    {
        pair.clearCollisionNoEvent();
        return;
    }

    Collision *collision = pair.getOrCreateCollisionNoEvent();

    collision->clearSupportState();

    collision->setIncidentObject(realIncidentCollider);
    collision->setReferenceObject(realIncidentCollider == incidentCollider ? referenceCollider : incidentCollider);

    // Ensure the normal points from reference toward incident
    Eigen::Vector2f directionVector = collision->getIncidentObject()->getCachedWorldPosition() -
                                      collision->getReferenceObject()->getCachedWorldPosition();
    if (minNormal.dot(directionVector) < 0)
    {
        minNormal = -minNormal;
    }

    // Store the corrected normal
    collision->setNormal(minNormal);

    calculateContactPoint(collision);
    // Float SAT can round a sub-ulp separating gap to overlap. Clipping must
    // produce an actual manifold before this pair contributes events or support.
    if (!collision->getContactPoints().count)
    {
        pair.clearCollisionNoEvent();
        return;
    }
    collision->updateSupportState(gravityVectorNormalized);
}

void PhysicsManager::calculateContactPoint(Collision *collision)
{
    const Collider *referenceCollider = collision->getReferenceObject();
    const Collider *incidentCollider = collision->getIncidentObject();
    const Eigen::Vector2f &normal = collision->getNormal();

    const Edge referenceEdge = referenceCollider->getEdgeWithNormal(normal);
    const Edge incidentEdge = incidentCollider->getEdgeWithNormal(-normal);

    ColliderType referenceType = referenceCollider->getColliderType();
    ColliderType incidentType = incidentCollider->getColliderType();

    if (incidentType == ColliderType::CircleCollider)
    {
        ClipPoints cp;
        cp.add(incidentEdge.max,
               makeContactFeature(referenceEdge.edgeNumber, NO_EDGE, incidentEdge.edgeNumber, NO_EDGE));
        collision->setContactPoints(cp);
        float max = normal.dot(referenceEdge.max);
        float separation = normal.dot(incidentEdge.max) - max;
        collision->getContactPoints().points[0].separation = separation;
        return;
    }
    else if (referenceType == ColliderType::CircleCollider)
    {
        ClipPoints cp;
        const Eigen::Vector2f circleCenter = referenceCollider->getGameObject()->worldPosition;
        const Eigen::Vector2f edgeVector = incidentEdge.getEdgeVector();
        const float edgeLengthSquared = edgeVector.squaredNorm();

        float t = 0.0f;
        if (edgeLengthSquared > 1e-12f)
        {
            t = std::clamp((circleCenter - incidentEdge.v1).dot(edgeVector) / edgeLengthSquared, 0.0f, 1.0f);
        }

        const Eigen::Vector2f closestPoint = incidentEdge.v1 + t * edgeVector;
        cp.add(closestPoint, makeContactFeature(referenceEdge.edgeNumber, NO_EDGE, incidentEdge.edgeNumber, NO_EDGE));
        collision->setContactPoints(cp);

        const float max = normal.dot(referenceEdge.max);
        const float separation = normal.dot(closestPoint) - max;
        collision->getContactPoints().points[0].separation = separation;
        return;
    }

    // the edge vector
    Eigen::Vector2f referenceEdgeVector = referenceEdge.getEdgeVector();
    referenceEdgeVector.normalize();

    ClipVertex incidentVertices[2];

    incidentVertices[0] = ClipVertex(
        incidentEdge.v1, makeContactFeature(NO_EDGE, NO_EDGE, getStartVertexAdjacentEdge(incidentEdge.edgeNumber),
                                            incidentEdge.edgeNumber));
    incidentVertices[1] = ClipVertex(
        incidentEdge.v2, makeContactFeature(NO_EDGE, NO_EDGE, getEndVertexAdjacentEdge(incidentEdge.edgeNumber),
                                            incidentEdge.edgeNumber));

    ClipVertex clippedVertices1[2];
    ClipVertex clippedVertices2[2];

    double o1 = referenceEdgeVector.dot(referenceEdge.v1);
    int clippedCount = clipSegmentToLine(clippedVertices1, incidentVertices, referenceEdgeVector, o1,
                                         getStartVertexAdjacentEdge(referenceEdge.edgeNumber));
    if (clippedCount < 2)
    {
        PLATFORMATOR_LOG("Clipping failed at first reference vertex\n");
        collision->generateFallbackContactPoint();
        return;
    }

    // clip whats left of the incident edge by the
    // second vertex of the reference edge
    // but we need to clip in the opposite direction
    // so we flip the direction and offset
    double o2 = referenceEdgeVector.dot(referenceEdge.v2);
    clippedCount = clipSegmentToLine(clippedVertices2, clippedVertices1, -referenceEdgeVector, -o2,
                                     getEndVertexAdjacentEdge(referenceEdge.edgeNumber));
    if (clippedCount < 2)
    {
        PLATFORMATOR_LOG("Clipping failed at second reference vertex\n");
        collision->generateFallbackContactPoint();
        return;
    }

    double max = normal.dot(referenceEdge.max);

    ClipPoints cp;
    for (int i = 0; i < clippedCount; ++i)
    {
        float separation = normal.dot(clippedVertices2[i].point) - max;
        if (separation <= 0.0f)
        {
            cp.add(clippedVertices2[i]);
        }
    }

    collision->setContactPoints(cp);

    ClipPointsWithData &contactPoints = collision->getContactPoints();
    for (size_t i = 0; i < contactPoints.count; i++)
    {
        float separation = normal.dot(cp.points[i].point) - max;
        contactPoints.points[i].separation = separation;
    }
}

void PhysicsManager::resolveCollisions(double timeDelta)
{
    PLATFORMATOR_BENCH_SCOPE(ResolveCollisions);

    float inverseTimeDelta = timeDelta > 0.0 ? static_cast<float>(1.0 / timeDelta) : 0.0f;
    dynamicCollisionScratch.clear();

    for (Collision *collision : activeCollisions)
    {
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

    drainBodyChanges();
    updateSleepingStates(timeDelta);

#if PLATFORMATOR_ENABLE_BENCHMARKS
    PLATFORMATOR_BENCH_SET_COUNTER(AwakeDynamicBodyCount, countAwakeDynamicBodies());
#endif
}

void PhysicsManager::preStepCollision(Collision *collision, float inverseTimeDelta)
{
    Rigidbody *rbA =
        (Rigidbody *)collision->getReferenceObject()->getGameObject()->getComponent(ComponentType::RIGID_BODY);
    Rigidbody *rbB =
        (Rigidbody *)collision->getIncidentObject()->getGameObject()->getComponent(ComponentType::RIGID_BODY);

    if (rbA == nullptr || rbB == nullptr)
    {
        return;
    }

    const auto impact = [&](Rigidbody *body, Rigidbody *other) {
        return other->getVelocity().squaredNorm() > LINEAR_SLEEP_THRESHOLD * LINEAR_SLEEP_THRESHOLD ||
               std::abs(other->getAngularVelocity()) > ANGULAR_SLEEP_THRESHOLD ||
               (other->getBodyType() == KINEMATIC &&
                (!other->getVelocity().isZero(0) || other->getAngularVelocity() != 0));
    };
    if (rbA->getIsSleeping() && impact(rbA, rbB))
        rbA->wakeUp();
    if (rbB->getIsSleeping() && impact(rbB, rbA))
        rbB->wakeUp();
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

        float baumgarteBias = -COLLISION_BIAS_FACTOR * inverseTimeDelta *
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
            rhs[i] = ((rbB->getVelocity() + crossSV(rbB->getAngularVelocity(), rB[i])) -
                      (rbA->getVelocity() + crossSV(rbA->getAngularVelocity(), rA[i])))
                         .dot(normal) -
                     contactPoints.points[i].bias;
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
            for (int i = 0; i < 2; ++i)
            {
                const Eigen::Vector2f impulse = (next[i] - previous[i]) * normal;
                rbA->applySolverImpulse(-impulse, rA[i]);
                rbB->applySolverImpulse(impulse, rB[i]);
                contactPoints.points[i].accumulatedNormalImpulse = next[i];
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

        float maxTangentImpulse = collision->solverFriction * contactPoints.points[i].accumulatedNormalImpulse;
        float newTangentImpulse = Clamp(oldTangentImpulse + tangentImpulse, -maxTangentImpulse, maxTangentImpulse);
        contactPoints.points[i].accumulatedTangentImpulse = newTangentImpulse;
        tangentImpulse = newTangentImpulse - oldTangentImpulse;

        Eigen::Vector2f contactImpulse = tangentImpulse * tangent;

        rbA->applySolverImpulse(-contactImpulse, rA);
        rbB->applySolverImpulse(contactImpulse, rB);
    }
}

void PhysicsManager::resolveKinematicCollision(const Collision *collision)
{
    Rigidbody *rbA =
        (Rigidbody *)collision->getReferenceObject()->getGameObject()->getComponent(ComponentType::RIGID_BODY);
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
        gameObject->queueTransform();
    }
}

void PhysicsManager::updateSleepingStates(double timeDelta)
{
    PLATFORMATOR_WORK_ADD(sleepBodies, sleepEligibleBodies.size());
    for (auto *body : sleepEligibleBodies)
    {
        auto *root = sleepRoot(body);
        root->islandQuiet &= body->getVelocity().squaredNorm() <= LINEAR_SLEEP_THRESHOLD * LINEAR_SLEEP_THRESHOLD &&
                             std::abs(body->getAngularVelocity()) <= ANGULAR_SLEEP_THRESHOLD;
        root->islandTimer = std::min(root->islandTimer, body->getSleepTimer());
    }
    for (auto *body : sleepEligibleBodies)
    {
        auto *root = sleepRoot(body);
        const auto timer = root->islandQuiet ? root->islandTimer + timeDelta : 0;
        if (timer >= SLEEP_DELAY)
            body->setIsSleeping(true);
        else
            body->setSleepTimer(timer);
    }
    drainBodyChanges();
}

void PhysicsManager::addPendingPhysicsEvent(const PhysicsEvent &event)
{
    pendingPhysicsEvents.push_back(event);
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
    std::exception_ptr error;
    for (const PhysicsEvent &event : pendingPhysicsEvents)
    {
        auto deliver = [&](Collider *receiver, Collider *other) {
            try
            {
                if (event.type == PhysicsEvent::COLLISION_ENTER)
                    receiver->triggerCollisionEnter(event.collision, other, timeDelta);
                else if (event.type == PhysicsEvent::COLLISION_STAY)
                    receiver->triggerCollisionStay(event.collision, other, timeDelta);
                else
                    receiver->triggerCollisionExit(other, timeDelta);
            }
            catch (...)
            {
                if (!error)
                    error = std::current_exception();
            }
        };
        deliver(event.colliderA, event.colliderB);
        deliver(event.colliderB, event.colliderA);
    }
    clearPendingPhysicsEvents();
    if (error)
        std::rethrow_exception(error);
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
