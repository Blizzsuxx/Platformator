#include "physicsmanager.h"
#include "debugdraw.h"

PhysicsManager::PhysicsManager()
    : rigidBodyComponents(), pendingColliderComponents(), pendingColliderSyncs(), activeCollisions(), grid(), gravityVector(GRAVITY_VECTOR_X, GRAVITY_VECTOR_Y), gravityVectorNormalized(gravityVector.normalized())
{
}

PhysicsManager::~PhysicsManager()
{
}

void PhysicsManager::applyPhysics(double timeDelta)
{
    // TODO: parallelize this loop
    for (Rigidbody *rigidBodyComponent : rigidBodyComponents)
    {
        rigidBodyComponent->applyForces(timeDelta, gravityVector);
    }
}

void PhysicsManager::applyMovement(double timeDelta)
{
    // TODO: parallelize this loop
    for (Rigidbody *rigidBodyComponent : rigidBodyComponents)
    {
        rigidBodyComponent->move(timeDelta);
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
    if (colliderComponent == nullptr || !colliderComponent->getGameObject()->getActive() || colliderComponent->getIsQueuedForAdd() || colliderComponent->getIsRegisteredInGrid())
    {
        return;
    }

    colliderComponent->markQueuedForAdd();
    pendingColliderComponents.push_back(colliderComponent);
}

void PhysicsManager::refreshColliderComponent(Collider *colliderComponent)
{
    if (colliderComponent == nullptr || !colliderComponent->getGameObject()->getActive() || !colliderComponent->getIsRegisteredInGrid())
    {
        return;
    }

    grid.removeCollider(colliderComponent);
    colliderComponent->setIsRegisteredInGrid(false);
    grid.addCollider(colliderComponent);
    colliderComponent->setIsRegisteredInGrid(true);
}

void PhysicsManager::queueColliderSync(Collider *colliderComponent)
{
    pendingColliderSyncs.push_back(colliderComponent);
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
    colliderComponent->clearQueuedForAdd();
    colliderComponent->removeSync();

    if (!colliderComponent->getIsRegisteredInGrid())
    {
        return;
    }

    grid.removeCollider(colliderComponent);
    colliderComponent->setIsRegisteredInGrid(false);
}

void PhysicsManager::flushPendingColliderSyncs()
{
    if (pendingColliderSyncs.empty())
    {
        return;
    }

    for (Collider *colliderComponent : pendingColliderSyncs)
    {
        if (!colliderComponent->getIsQueuedForSync())
        {
            continue;
        }

        grid.syncCollider(colliderComponent);
    }

    pendingColliderSyncs.clear();
}

void PhysicsManager::flushPendingColliderComponents()
{
    if (pendingColliderComponents.empty())
    {
        return;
    }

    for (Collider *colliderComponent : pendingColliderComponents)
    {
        if (!colliderComponent->getIsQueuedForAdd())
        {
            continue;
        }

        colliderComponent->clearQueuedForAdd();
        grid.addCollider(colliderComponent);
        colliderComponent->setIsRegisteredInGrid(true);
    }

    pendingColliderComponents.clear();
}

void PhysicsManager::broadPhase()
{
    flushPendingColliderSyncs();
    flushPendingColliderComponents();
}

void PhysicsManager::narrowPhase()
{
    activeCollisions.clear();

    for (const ColliderPair *pair : *grid.getPendingNarrowPhasePairs())
    {
        pair->setIsQueuedForNarrowPhase(false);
        pair->setNarrowPhaseQueueIndex(SIZE_MAX);

        Collider *objectA = pair->getObjectA();
        Collider *objectB = pair->getObjectB();

        if (objectA->getGameObject()->getIsMarkedForDeletion() || objectB->getGameObject()->getIsMarkedForDeletion())
        {
            continue;
        }
        if (!objectA->getGameObject()->getActive() || !objectB->getGameObject()->getActive())
        {
            continue;
        }

        if (!pair->shouldUpdate())
        {
            pair->triggerCollisionStay();
        }
        else
        {
            satCreateCollision(*pair);
        }

        if (pair->getCollision() != nullptr)
        {
            activeCollisions.push_back(pair->getCollision());
            DebugDraw::getInstance().addCollisionDebugObject(*pair->getCollision());
        }
    }

    grid.clearPendingNarrowPhasePairs();
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
        pair.clearCollision();
        return;
    }

    normals = incidentCollider->getNormals(referenceCollider);

    if (checkProjections(normals, incidentCollider, referenceCollider, minOverlap, minNormal, realIncidentCollider) == false)
    {
        pair.clearCollision();
        return;
    }

    Collision *collision = pair.getOrCreateCollision();

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
    // Approximate contact point as the midpoint of the incident projection on the collision normal
    const Collider *referenceCollider = collision->getReferenceObject();
    const Collider *incidentCollider = collision->getIncidentObject();
    const Eigen::Vector2f &normal = collision->getNormal();

    const Edge referenceEdge = referenceCollider->getEdgeWithNormal(normal);
    const Edge incidentEdge = incidentCollider->getEdgeWithNormal(-normal);

    // bool flipped = false;
    // float dotBetweenReferenceAndNormal = abs(referenceEdge.direction.dot(normal));
    // float dotBetweenIncidentAndNormal = abs(incidentEdge.direction.dot(normal));

    // // not sure if this is even needed
    // if (dotBetweenIncidentAndNormal < dotBetweenReferenceAndNormal)
    // {
    //     std::swap(referenceEdge, incidentEdge);
    //     flipped = true;
    //     printf("Flipped edges for contact point calculation\n");
    // }

    // the edge vector
    Eigen::Vector2f referenceEdgeVector = referenceEdge.getEdgeVector();
    referenceEdgeVector.normalize();

    double o1 = referenceEdgeVector.dot(referenceEdge.v1);
    // clip the incident edge by the first
    // vertex of the reference edge
    ClipPoints cp = clip(incidentEdge.v1, incidentEdge.v2, referenceEdgeVector, o1);
    // if we dont have 2 points left then fail
    if (cp.count < 2)
    {
        collision->setContactPoints(ClipPoints());
        printf("Clipping failed at first reference vertex\n");
        return;
    }

    // clip whats left of the incident edge by the
    // second vertex of the reference edge
    // but we need to clip in the opposite direction
    // so we flip the direction and offset
    double o2 = (referenceEdgeVector.dot(referenceEdge.v2));
    cp = clip(cp.points[0], cp.points[1], -referenceEdgeVector, -o2);
    // if we dont have 2 points left then fail
    if (cp.count < 2)
    {
        collision->setContactPoints(ClipPoints());
        printf("Clipping failed at second reference vertex\n");
        return;
    }

    // get the reference edge normal
    // Vector2 refNorm = ref.cross(-1.0);
    // if we had to flip the incident and reference edges
    // then we need to flip the reference edge normal to
    // clip properly
    // get the largest depth
    double max = normal.dot(referenceEdge.max);
    // make sure the final points are not past this maximum

    size_t indexForRemoval = 0;
    if (normal.dot(cp.points[indexForRemoval]) - max > 0.0)
    {
        cp.remove(indexForRemoval);
        indexForRemoval--;
    }
    indexForRemoval++;
    if (normal.dot(cp.points[indexForRemoval]) - max > 0.0)
    {
        cp.remove(indexForRemoval);
    }

    collision->setContactPoints(cp);

    ClipPointsWithData &contactPoints = collision->getContactPoints();
    for (size_t i = 0; i < contactPoints.count; i++)
    {
        float separation = normal.dot(cp.points[i]) - max;
        contactPoints.points[i].separation = separation;
    }
}

void PhysicsManager::resolveCollisions(double timeDelta)
{
    float inverseTimeDelta = timeDelta > 0.0 ? static_cast<float>(1.0 / timeDelta) : 0.0f;

    for (Collision *collision : activeCollisions)
    {
        const Collider *referenceCollider = collision->getReferenceObject();
        const Collider *incidentCollider = collision->getIncidentObject();

        if (collision->shouldResolve())
        {
            preStepCollision(collision, inverseTimeDelta);
        }
    }

    for (int iteration = 0; iteration < COLLISION_SOLVER_ITERATIONS; iteration++)
    {
        for (Collision *collision : activeCollisions)
        {
            const Collider *referenceCollider = collision->getReferenceObject();
            const Collider *incidentCollider = collision->getIncidentObject();

            if (collision->shouldResolve())
            {
                resolveCollision(collision);
            }
        }
    }

    updateSleepingStates(timeDelta);
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

    const Eigen::Vector2f &positionA = rbA->getGameObject()->getPosition();
    const Eigen::Vector2f &positionB = rbB->getGameObject()->getPosition();

    const Eigen::Vector2f &normal = collision->getNormal();
    Eigen::Vector2f tangent = crossSV(1.0f, normal);
    float inverseMassSum = invMassA + invMassB;
    ClipPointsWithData &contactPoints = collision->getContactPoints();

    for (size_t i = 0; i < contactPoints.count; i++)
    {
        Eigen::Vector2f rA = contactPoints.points[i].point - positionA;
        Eigen::Vector2f rB = contactPoints.points[i].point - positionB;
        float rnA = rA.dot(normal);
        float rnB = rB.dot(normal);
        float kNormal = inverseMassSum + invInertiaA * (rA.dot(rA) - rnA * rnA) + invInertiaB * (rB.dot(rB) - rnB * rnB);
        contactPoints.points[i].massNormal = 1.0f / kNormal;

        float rtA = rA.dot(tangent);
        float rtB = rB.dot(tangent);
        float kTangent = inverseMassSum + invInertiaA * (rA.dot(rA) - rtA * rtA) + invInertiaB * (rB.dot(rB) - rtB * rtB);
        contactPoints.points[i].massTangent = 1.0f / kTangent;

        contactPoints.points[i].bias = -COLLISION_BIAS_FACTOR * inverseTimeDelta * std::min(0.0f, contactPoints.points[i].separation + COLLISION_ALLOWED_PENETRATION);

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

    const Eigen::Vector2f &positionA = gameObjectA->getPosition();
    const Eigen::Vector2f &positionB = gameObjectB->getPosition();

    float invMassA = rbA->getInverseMass();
    float invMassB = rbB->getInverseMass();

    float invInertiaA = rbA->getInverseMomentOfInertia();
    float invInertiaB = rbB->getInverseMomentOfInertia();
    const Eigen::Vector2f &normal = collision->getNormal();
    Eigen::Vector2f tangent = crossSV(1.0f, normal);
    ClipPointsWithData &contactPoints = collision->getContactPoints();

    // TODO: paralelize this
    for (size_t i = 0; i < contactPoints.count; i++)
    {
        Eigen::Vector2f rA = contactPoints.points[i].point - positionA;
        Eigen::Vector2f rB = contactPoints.points[i].point - positionB;

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