#include "physicsmanager.h"

#include "boxcollider.h"
#include "benchmark.h"
#include "constants.h"
#include "debugdraw.h"
#include "oneapi/tbb.h"

#include <limits>

PhysicsManager::PhysicsManager()
    : rigidBodyComponents(), colliderComponents(), activeCollisions(), pendingPhysicsEvents(), grid(), gravityVector(GRAVITY_VECTOR_X, GRAVITY_VECTOR_Y), gravityVectorNormalized(gravityVector.normalized())
{
}

PhysicsManager::~PhysicsManager()
{
}

void PhysicsManager::applyPhysics(double timeDelta)
{
    tbb::parallel_for(size_t(0), rigidBodyComponents.size(), [&](size_t i)
                      { rigidBodyComponents[i]->applyForces(timeDelta, gravityVector); });
}

void PhysicsManager::applyMovement(double timeDelta)
{
    tbb::parallel_for(size_t(0), rigidBodyComponents.size(), [&](size_t i)
                      { rigidBodyComponents[i]->move(timeDelta); });
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
    if (rigidBodyComponent == nullptr || rigidBodyComponent->getIsRegisteredInPhysicsManager() || !rigidBodyComponent->getGameObject()->getActive())
    {
        return;
    }

    rigidBodyComponent->setPhysicsManagerIndex(rigidBodyComponents.size());
    rigidBodyComponent->setIsRegisteredInPhysicsManager(true);
    rigidBodyComponents.push_back(rigidBodyComponent);
}

void PhysicsManager::addColliderComponent(Collider *colliderComponent)
{
    if (colliderComponent == nullptr || colliderComponent->getIsRegisteredInPhysicsManager() || !colliderComponent->getGameObject()->getActive())
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
}

void PhysicsManager::broadPhase()
{
    PLATFORMATOR_BENCH_SCOPE(BroadPhase);

    flushPendingColliderOperations();

    PLATFORMATOR_BENCH_SET_COUNTER(OccupiedCellCount, grid.getCells().size());
    PLATFORMATOR_BENCH_SET_COUNTER(CandidatePairCount, grid.getCandidatePairCount());
}

void PhysicsManager::narrowPhase()
{
    PLATFORMATOR_BENCH_SCOPE(NarrowPhase);

    activeCollisions.clear();

    const std::vector<const ColliderPair *> &pendingPairs = *grid.getPendingNarrowPhasePairs();

    if (pendingPairs.size() < NARROW_PHASE_PARALLEL_THRESHOLD)
    {
        NarrowPhaseLocalResults localResults;
        localResults.activeCollisions.reserve(pendingPairs.size());
        localResults.physicsEvents.reserve(pendingPairs.size());

        for (const ColliderPair *pair : pendingPairs)
        {
            processNarrowPhasePair(pair, localResults.activeCollisions, localResults.physicsEvents);
        }

        activeCollisions = std::move(localResults.activeCollisions);
        pendingPhysicsEvents.insert(pendingPhysicsEvents.end(), localResults.physicsEvents.begin(), localResults.physicsEvents.end());
    }
    else
    {
        tbb::enumerable_thread_specific<NarrowPhaseLocalResults> localResults;

        tbb::parallel_for(size_t(0), pendingPairs.size(), [&](size_t i)
                          { processNarrowPhasePair(pendingPairs[i], localResults.local().activeCollisions, localResults.local().physicsEvents); });

        size_t totalCollisionCount = 0;
        size_t totalEventCount = 0;
        for (NarrowPhaseLocalResults &results : localResults)
        {
            totalCollisionCount += results.activeCollisions.size();
            totalEventCount += results.physicsEvents.size();
        }

        activeCollisions.reserve(totalCollisionCount);
        pendingPhysicsEvents.reserve(pendingPhysicsEvents.size() + totalEventCount);

        for (NarrowPhaseLocalResults &results : localResults)
        {
            activeCollisions.insert(activeCollisions.end(), results.activeCollisions.begin(), results.activeCollisions.end());
            pendingPhysicsEvents.insert(pendingPhysicsEvents.end(), results.physicsEvents.begin(), results.physicsEvents.end());
        }
    }

#if PLATFORMATOR_ENABLE_DEBUG_TOOLS
    for (Collision *collision : activeCollisions)
    {
        DebugDraw::getInstance().addCollisionDebugObject(*collision);
    }
#endif

#if PLATFORMATOR_ENABLE_BENCHMARKS
    PLATFORMATOR_BENCH_SET_COUNTER(PendingNarrowPhasePairCount, pendingPairs.size());
    PLATFORMATOR_BENCH_SET_COUNTER(ActiveCollisionCount, activeCollisions.size());

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
    size_t awakeDynamicBodyCount = 0;

    for (Rigidbody *rigidBodyComponent : rigidBodyComponents)
    {
        if (rigidBodyComponent->getBodyType() == BodyType::DYNAMIC && rigidBodyComponent->getGameObject()->getActive() && !rigidBodyComponent->getIsSleeping())
        {
            awakeDynamicBodyCount++;
        }
    }

    return awakeDynamicBodyCount;
}
#endif

void PhysicsManager::processNarrowPhasePair(const ColliderPair *pair, std::vector<Collision *> &localCollisions, std::vector<PhysicsEvent> &localEvents)
{
    pair->setIsQueuedForNarrowPhase(false);
    pair->setNarrowPhaseQueueIndex(SIZE_MAX);

    Collider *objectA = pair->getObjectA();
    Collider *objectB = pair->getObjectB();

    if (objectA->getGameObject()->getIsMarkedForDeletion() || objectB->getGameObject()->getIsMarkedForDeletion())
    {
        return;
    }
    if (!objectA->getGameObject()->getActive() || !objectB->getGameObject()->getActive())
    {
        return;
    }

    Collision *existingCollision = pair->getCollision();

    if (!pair->shouldUpdate())
    {
        localEvents.emplace_back(makePhysicsEvent(PhysicsEvent::COLLISION_STAY, pair, existingCollision));

        if (existingCollision != nullptr)
        {
            localCollisions.push_back(existingCollision);
        }

        return;
    }

    const bool hadCollision = existingCollision != nullptr;
    satCreateCollision(*pair);

    Collision *updatedCollision = pair->getCollision();

    if (hadCollision)
    {
        if (updatedCollision == nullptr)
        {
            localEvents.emplace_back(makePhysicsEvent(PhysicsEvent::COLLISION_EXIT, pair, nullptr));
            return;
        }

        localEvents.emplace_back(makePhysicsEvent(PhysicsEvent::COLLISION_STAY, pair, updatedCollision));
    }
    else if (updatedCollision != nullptr)
    {
        localEvents.emplace_back(makePhysicsEvent(PhysicsEvent::COLLISION_ENTER, pair, updatedCollision));
    }

    if (updatedCollision != nullptr)
    {
        localCollisions.push_back(updatedCollision);
    }
}

bool PhysicsManager::checkProjections(const std::vector<Eigen::Vector2f> &normals, const Collider *referenceCollider, const Collider *incidentCollider, float &minOverlap, Eigen::Vector2f &minNormal, const Collider *&realIncidentCollider)
{
    for (long unsigned int i = 0; i < normals.size(); i++)
    {
        const Eigen::Vector2f &normal = normals[i];
        auto projections1 = referenceCollider->projectOntoAxis(normal); // we are using the index on the owner of the normal to further optimize projection
        auto projections2 = incidentCollider->projectOntoAxis(normal);  // TODO: see if it's better to approximate from the perspective of the incident or reference object

        if (projections1.y() < projections2.x() || projections2.y() < projections1.x())
        {
            return false;
        }
        else
        {
            float overlap = std::min(projections1.y(), projections2.y()) - std::max(projections1.x(), projections2.x());
            if (overlap < minOverlap)
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

    float minOverlap = std::numeric_limits<float>::max();
    Eigen::Vector2f minNormal;
    const Collider *realIncidentCollider = incidentCollider;

    std::vector<Eigen::Vector2f> normals = referenceCollider->getNormals(incidentCollider);
    if (checkProjections(normals, referenceCollider, incidentCollider, minOverlap, minNormal, realIncidentCollider) == false)
    {
        pair.clearCollisionNoEvent();
        return;
    }

    normals = incidentCollider->getNormals(referenceCollider);

    if (checkProjections(normals, incidentCollider, referenceCollider, minOverlap, minNormal, realIncidentCollider) == false)
    {
        pair.clearCollisionNoEvent();
        return;
    }

    Collision *collision = pair.getOrCreateCollisionNoEvent();

    collision->clearSupportState();

    collision->setIncidentObject(realIncidentCollider);
    collision->setReferenceObject(realIncidentCollider == incidentCollider ? referenceCollider : incidentCollider);

    // Ensure the normal points from reference toward incident
    auto directionVector = collision->getIncidentObject()->getGameObject()->getPosition() - collision->getReferenceObject()->getGameObject()->getPosition();
    if (minNormal.dot(directionVector) < 0)
    {
        minNormal = -minNormal;
    }

    // Store the corrected normal
    collision->setNormal(minNormal);

    calculateContactPoint(collision);
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
        cp.add(incidentEdge.max, makeContactFeature(referenceEdge.edgeNumber, NO_EDGE, incidentEdge.edgeNumber, NO_EDGE));
        collision->setContactPoints(cp);
        float max = normal.dot(referenceEdge.max);
        float separation = normal.dot(incidentEdge.max) - max;
        collision->getContactPoints().points[0].separation = separation;
        return;
    }
    else if (referenceType == ColliderType::CircleCollider)
    {
        ClipPoints cp;
        const Eigen::Vector2f circleCenter = referenceCollider->getGameObject()->getPosition();
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

    incidentVertices[0] = ClipVertex(incidentEdge.v1, makeContactFeature(NO_EDGE, NO_EDGE, getStartVertexAdjacentEdge(incidentEdge.edgeNumber), incidentEdge.edgeNumber));
    incidentVertices[1] = ClipVertex(incidentEdge.v2, makeContactFeature(NO_EDGE, NO_EDGE, getEndVertexAdjacentEdge(incidentEdge.edgeNumber), incidentEdge.edgeNumber));

    ClipVertex clippedVertices1[2];
    ClipVertex clippedVertices2[2];

    double o1 = referenceEdgeVector.dot(referenceEdge.v1);
    int clippedCount = clipSegmentToLine(clippedVertices1, incidentVertices, referenceEdgeVector, o1, getStartVertexAdjacentEdge(referenceEdge.edgeNumber));
    if (clippedCount < 2)
    {
        PLATFORMATOR_LOG("Clipping failed at first reference vertex\n");
        return;
    }

    // clip whats left of the incident edge by the
    // second vertex of the reference edge
    // but we need to clip in the opposite direction
    // so we flip the direction and offset
    double o2 = referenceEdgeVector.dot(referenceEdge.v2);
    clippedCount = clipSegmentToLine(clippedVertices2, clippedVertices1, -referenceEdgeVector, -o2, getEndVertexAdjacentEdge(referenceEdge.edgeNumber));
    if (clippedCount < 2)
    {
        PLATFORMATOR_LOG("Clipping failed at second reference vertex\n");
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
    std::vector<Collision *> dynamicCollisions;

    for (Collision *collision : activeCollisions)
    {
        const Collider *referenceCollider = collision->getReferenceObject();
        const Collider *incidentCollider = collision->getIncidentObject();

        Collision::CollisionType resolutionType = collision->getResolutionType();

        switch (resolutionType)
        {
        case Collision::CollisionType::DYNAMIC:
            preStepCollision(collision, inverseTimeDelta);
            dynamicCollisions.push_back(collision);
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
        for (Collision *collision : dynamicCollisions)
        {
            resolveCollision(collision);
        }
    }

    updateSleepingStates(timeDelta);

#if PLATFORMATOR_ENABLE_BENCHMARKS
    PLATFORMATOR_BENCH_SET_COUNTER(AwakeDynamicBodyCount, countAwakeDynamicBodies());
#endif
}

void PhysicsManager::preStepCollision(Collision *collision, float inverseTimeDelta)
{
    Rigidbody *rbA = (Rigidbody *)collision->getReferenceObject()->getGameObject()->getComponent(ComponentType::RIGID_BODY);
    Rigidbody *rbB = (Rigidbody *)collision->getIncidentObject()->getGameObject()->getComponent(ComponentType::RIGID_BODY);

    if (rbA == nullptr || rbB == nullptr)
    {
        return;
    }

    float invMassA = rbA->getInverseMass();
    float invMassB = rbB->getInverseMass();
    float invInertiaA = rbA->getInverseMomentOfInertia();
    float invInertiaB = rbB->getInverseMomentOfInertia();

    Eigen::Vector2f positionA = rbA->getGameObject()->getPosition();
    Eigen::Vector2f positionB = rbB->getGameObject()->getPosition();

    const Eigen::Vector2f &normal = collision->getNormal();
    Eigen::Vector2f tangent = crossSV(1.0f, normal);
    float inverseMassSum = invMassA + invMassB;
    ClipPointsWithData &contactPoints = collision->getContactPoints();

    for (size_t i = 0; i < contactPoints.count; i++)
    {
        Eigen::Vector2f rA = contactPoints.points[i].point.point - positionA;
        Eigen::Vector2f rB = contactPoints.points[i].point.point - positionB;
        float rnA = rA.dot(normal);
        float rnB = rB.dot(normal);
        float kNormal = inverseMassSum + invInertiaA * (rA.dot(rA) - rnA * rnA) + invInertiaB * (rB.dot(rB) - rnB * rnB);
        contactPoints.points[i].massNormal = 1.0f / kNormal;

        float rtA = rA.dot(tangent);
        float rtB = rB.dot(tangent);
        float kTangent = inverseMassSum + invInertiaA * (rA.dot(rA) - rtA * rtA) + invInertiaB * (rB.dot(rB) - rtB * rtB);
        contactPoints.points[i].massTangent = 1.0f / kTangent;

        float baumgarteBias = -COLLISION_BIAS_FACTOR * inverseTimeDelta * std::min(0.0f, contactPoints.points[i].separation + COLLISION_ALLOWED_PENETRATION);

        Eigen::Vector2f relativeVelocity = (rbB->getVelocity() + crossSV(rbB->getAngularVelocity(), rB)) - (rbA->getVelocity() + crossSV(rbA->getAngularVelocity(), rA));
        float vn = relativeVelocity.dot(normal);
        float restitutionBias = 0.0f;
        if (vn < -RESTITUTION_VELOCITY_THRESHOLD)
        {
            restitutionBias = -collision->getRestitution() * vn;
        }

        contactPoints.points[i].bias = baumgarteBias + restitutionBias;

        Eigen::Vector2f accumulatedImpulse = contactPoints.points[i].accumulatedNormalImpulse * normal + contactPoints.points[i].accumulatedTangentImpulse * tangent;
        rbA->setVelocity(rbA->getVelocity() - accumulatedImpulse * invMassA);
        rbB->setVelocity(rbB->getVelocity() + accumulatedImpulse * invMassB);
        rbA->setAngularVelocity(rbA->getAngularVelocity() - invInertiaA * cross2D(rA, accumulatedImpulse));
        rbB->setAngularVelocity(rbB->getAngularVelocity() + invInertiaB * cross2D(rB, accumulatedImpulse));
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

    Eigen::Vector2f positionA = gameObjectA->getPosition();
    Eigen::Vector2f positionB = gameObjectB->getPosition();

    float invMassA = rbA->getInverseMass();
    float invMassB = rbB->getInverseMass();

    float invInertiaA = rbA->getInverseMomentOfInertia();
    float invInertiaB = rbB->getInverseMomentOfInertia();
    const Eigen::Vector2f &normal = collision->getNormal();
    Eigen::Vector2f tangent = crossSV(1.0f, normal);
    ClipPointsWithData &contactPoints = collision->getContactPoints();

    // TODO: paralelize this
    // or not
    for (size_t i = 0; i < contactPoints.count; i++)
    {
        Eigen::Vector2f rA = contactPoints.points[i].point.point - positionA;
        Eigen::Vector2f rB = contactPoints.points[i].point.point - positionB;

        Eigen::Vector2f relativeVelocity = (rbB->getVelocity() + crossSV(rbB->getAngularVelocity(), rB)) - (rbA->getVelocity() + crossSV(rbA->getAngularVelocity(), rA));

        float vn = relativeVelocity.dot(normal);
        float normalImpulse = contactPoints.points[i].massNormal * (-vn + contactPoints.points[i].bias);

        float accumulatedNormalImpulse = contactPoints.points[i].accumulatedNormalImpulse;
        float maxNormalImpulse = std::max(accumulatedNormalImpulse + normalImpulse, 0.0f);
        contactPoints.points[i].accumulatedNormalImpulse = maxNormalImpulse;
        normalImpulse = maxNormalImpulse - accumulatedNormalImpulse;

        Eigen::Vector2f impulse = normalImpulse * normal;

        rbA->setVelocity(rbA->getVelocity() - impulse * invMassA);
        rbB->setVelocity(rbB->getVelocity() + impulse * invMassB);
        rbA->setAngularVelocity(rbA->getAngularVelocity() - invInertiaA * cross2D(rA, impulse));
        rbB->setAngularVelocity(rbB->getAngularVelocity() + invInertiaB * cross2D(rB, impulse));

        relativeVelocity = (rbB->getVelocity() + crossSV(rbB->getAngularVelocity(), rB)) - (rbA->getVelocity() + crossSV(rbA->getAngularVelocity(), rA));

        float vt = relativeVelocity.dot(tangent);
        float tangentImpulse = contactPoints.points[i].massTangent * (-vt);

        float oldTangentImpulse = contactPoints.points[i].accumulatedTangentImpulse;

        float maxTangentImpulse = collision->getFriction() * contactPoints.points[i].accumulatedNormalImpulse;
        float newTangentImpulse = Clamp(oldTangentImpulse + tangentImpulse, -maxTangentImpulse, maxTangentImpulse);
        contactPoints.points[i].accumulatedTangentImpulse = newTangentImpulse;
        tangentImpulse = newTangentImpulse - oldTangentImpulse;

        Eigen::Vector2f contactImpulse = tangentImpulse * tangent;

        rbA->setVelocity(rbA->getVelocity() - contactImpulse * invMassA);
        rbB->setVelocity(rbB->getVelocity() + contactImpulse * invMassB);
        rbA->setAngularVelocity(rbA->getAngularVelocity() - invInertiaA * cross2D(rA, contactImpulse));
        rbB->setAngularVelocity(rbB->getAngularVelocity() + invInertiaB * cross2D(rB, contactImpulse));
    }
}

void PhysicsManager::resolveKinematicCollision(const Collision *collision)
{
    Rigidbody *rbA = (Rigidbody *)collision->getReferenceObject()->getGameObject()->getComponent(ComponentType::RIGID_BODY);
    Rigidbody *rbB = (Rigidbody *)collision->getIncidentObject()->getGameObject()->getComponent(ComponentType::RIGID_BODY);

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

void PhysicsManager::resolveKinematicBodyAgainstNormal(Rigidbody *rigidBody, const Eigen::Vector2f &approachNormal, float correctionDistance)
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
        gameObject->setPosition(gameObject->getPosition() - correctionDistance * approachNormal);
    }
}

void PhysicsManager::updateSleepingStates(double timeDelta)
{
    float linearSleepThresholdSquared = LINEAR_SLEEP_THRESHOLD * LINEAR_SLEEP_THRESHOLD;

    for (Rigidbody *rigidBodyComponent : rigidBodyComponents)
    {
        if (rigidBodyComponent == nullptr || rigidBodyComponent->getBodyType() != BodyType::DYNAMIC || !rigidBodyComponent->getGameObject()->getActive())
        {
            continue;
        }

        if (rigidBodyComponent->getIsSleeping())
        {
            if (!rigidBodyComponent->hasSupportContact())
            {
                rigidBodyComponent->wakeUp();
            }
            continue;
        }

        if (!rigidBodyComponent->hasSupportContact())
        {
            rigidBodyComponent->setSleepTimer(0.0);
            continue;
        }

        if (rigidBodyComponent->getVelocity().squaredNorm() > linearSleepThresholdSquared || std::abs(rigidBodyComponent->getAngularVelocity()) > ANGULAR_SLEEP_THRESHOLD)
        {
            rigidBodyComponent->setSleepTimer(0.0);
            continue;
        }

        double sleepTimer = rigidBodyComponent->getSleepTimer() + timeDelta;
        if (sleepTimer >= SLEEP_DELAY)
        {
            rigidBodyComponent->setIsSleeping(true);
        }
        else
        {
            rigidBodyComponent->setSleepTimer(sleepTimer);
        }
    }
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
    if (pendingPhysicsEvents.empty())
    {
        return;
    }

    for (const PhysicsEvent &event : pendingPhysicsEvents)
    {
        if (event.type == PhysicsEvent::COLLISION_ENTER)
        {
            if (event.colliderA != nullptr)
            {
                event.colliderA->triggerCollisionEnter(event.collision, event.colliderB, timeDelta);
            }
            if (event.colliderB != nullptr)
            {
                event.colliderB->triggerCollisionEnter(event.collision, event.colliderA, timeDelta);
            }
        }
        else if (event.type == PhysicsEvent::COLLISION_STAY)
        {
            if (event.colliderA != nullptr)
            {
                event.colliderA->triggerCollisionStay(event.collision, event.colliderB, timeDelta);
            }
            if (event.colliderB != nullptr)
            {
                event.colliderB->triggerCollisionStay(event.collision, event.colliderA, timeDelta);
            }
        }
        else if (event.type == PhysicsEvent::COLLISION_EXIT)
        {
            if (event.colliderA != nullptr)
            {
                event.colliderA->triggerCollisionExit(event.colliderB, timeDelta);
            }
            if (event.colliderB != nullptr)
            {
                event.colliderB->triggerCollisionExit(event.colliderA, timeDelta);
            }
        }
    }

    clearPendingPhysicsEvents();
}

const Eigen::Vector2f &PhysicsManager::getGravityVector() const
{
    return gravityVector;
}

const Eigen::Vector2f &PhysicsManager::getGravityVectorNormalized() const
{
    return gravityVectorNormalized;
}

PhysicsEvent PhysicsManager::makePhysicsEvent(PhysicsEvent::EventType type, const ColliderPair *pair, Collision *collision)
{
    return PhysicsEvent{type, collision, pair->getObjectA(), pair->getObjectB()};
}