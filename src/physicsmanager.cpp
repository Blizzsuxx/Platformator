#include "physicsmanager.h"
#include "debugdraw.h"

PhysicsManager::PhysicsManager()
    : rigidBodyComponents(), pendingColliderComponents(), pendingColliderSyncs(), activeCollisions(), grid(), gravityVector(GRAVITY_VECTOR_X, GRAVITY_VECTOR_Y)
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
        rigidBodyComponent->setIsSupportedThisFrame(false);
        rigidBodyComponent->applyGravity(gravityVector);
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

bool PhysicsManager::checkProjections(const std::vector<Eigen::Vector2f> &normals, const Collider *referenceCollider, const Collider *incidentCollider, float &minOverlap, Eigen::Vector2f &minNormal, Eigen::Vector2f &incidentProjection, const Collider *&realIncidentCollider)
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
                incidentProjection = projections2;
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
    Eigen::Vector2f incidentProjection;
    const Collider *realIncidentCollider = incidentCollider;

    std::vector<Eigen::Vector2f> normals = referenceCollider->getNormals(incidentCollider);
    if (checkProjections(normals, referenceCollider, incidentCollider, minOverlap, minNormal, incidentProjection, realIncidentCollider) == false)
    {
        pair.clearCollision();
        return;
    }

    normals = incidentCollider->getNormals(referenceCollider);

    if (checkProjections(normals, incidentCollider, referenceCollider, minOverlap, minNormal, incidentProjection, realIncidentCollider) == false)
    {
        pair.clearCollision();
        return;
    }

    Collision *collision = pair.getOrCreateCollision();

    collision->setIncidentObject(realIncidentCollider);
    collision->setReferenceObject(realIncidentCollider == incidentCollider ? referenceCollider : incidentCollider);

    // Ensure the normal points from reference toward incident
    auto directionVector = collision->getIncidentObject()->getGameObject()->getPosition() - collision->getReferenceObject()->getGameObject()->getPosition();
    if (minNormal.dot(directionVector) < 0)
    {
        minNormal *= -1.0f;
    }

    // Store the corrected normal
    collision->setNormal(minNormal);
    collision->setPenetration(minOverlap);

    // Contact point: incident's near surface, offset halfway into the overlap
    float incidentHalfExtent = (incidentProjection.y() - incidentProjection.x()) / 2.0f;
    collision->setContactPoint(
        realIncidentCollider->getGameObject()->getPosition() - minNormal * (incidentHalfExtent - minOverlap / 2.0f));
}

void PhysicsManager::resolveCollisions(double timeDelta)
{
    for (Collision *collision : activeCollisions)
    {
        const Collider *referenceCollider = collision->getReferenceObject();
        const Collider *incidentCollider = collision->getIncidentObject();

        if (!referenceCollider->getIsTrigger() && !incidentCollider->getIsTrigger())
        {
            resolveCollision(collision);
        }
    }

    updateSleepingStates(timeDelta);
}

void PhysicsManager::resolveCollision(const Collision *collision)
{
    // sequential impulses with angular velocity
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

    if (invMassA + invMassB == 0.0f)
    {
        return; // both static
    }

    Eigen::Vector2f normal = collision->getNormal();
    Eigen::Vector2f contactPoint = collision->getContactPoint();

    // Lever arms from center of mass to contact point
    Eigen::Vector2f rA = contactPoint - rbA->getGameObject()->getPosition();
    Eigen::Vector2f rB = contactPoint - rbB->getGameObject()->getPosition();

    // Relative velocity at contact point (includes angular contribution)
    Eigen::Vector2f vA = rbA->getVelocity() + crossSV(rbA->getAngularVelocity(), rA);
    Eigen::Vector2f vB = rbB->getVelocity() + crossSV(rbB->getAngularVelocity(), rB);
    Eigen::Vector2f relativeVelocity = vB - vA;
    float velocityAlongNormal = relativeVelocity.dot(normal);

    if (velocityAlongNormal > 0)
    {
        return; // separating
    }

    // --- Normal impulse (restitution) ---
    float e = std::min(rbA->getRestitution(), rbB->getRestitution());

    float rAxN = cross2D(rA, normal);
    float rBxN = cross2D(rB, normal);
    float effectiveMass = invMassA + invMassB + rAxN * rAxN * invInertiaA + rBxN * rBxN * invInertiaB;

    float jN = -(1.0f + e) * velocityAlongNormal;
    jN /= effectiveMass;

    Eigen::Vector2f normalImpulse = jN * normal;
    rbA->setVelocity(rbA->getVelocity() - normalImpulse * invMassA);
    rbB->setVelocity(rbB->getVelocity() + normalImpulse * invMassB);
    rbA->setAngularVelocity(rbA->getAngularVelocity() - cross2D(rA, normalImpulse) * invInertiaA);
    rbB->setAngularVelocity(rbB->getAngularVelocity() + cross2D(rB, normalImpulse) * invInertiaB);

    // --- Friction impulse (Coulomb friction) ---
    // Recompute relative velocity at contact point after normal impulse
    vA = rbA->getVelocity() + crossSV(rbA->getAngularVelocity(), rA);
    vB = rbB->getVelocity() + crossSV(rbB->getAngularVelocity(), rB);
    relativeVelocity = vB - vA;

    // Tangent vector (velocity component along surface)
    Eigen::Vector2f tangent = relativeVelocity - relativeVelocity.dot(normal) * normal;
    float tangentLength = tangent.norm();
    if (tangentLength > 1e-6f)
    {
        tangent /= tangentLength; // normalize

        float rAxT = cross2D(rA, tangent);
        float rBxT = cross2D(rB, tangent);
        float effectiveMassT = invMassA + invMassB + rAxT * rAxT * invInertiaA + rBxT * rBxT * invInertiaB;

        float jT = -relativeVelocity.dot(tangent);
        jT /= effectiveMassT;

        // Coulomb's law: clamp friction impulse to mu * normal impulse
        float mu = std::sqrt(rbA->getFriction() * rbB->getFriction());

        Eigen::Vector2f frictionImpulse;
        if (std::abs(jT) < jN * mu)
        {
            // Static friction
            frictionImpulse = jT * tangent;
        }
        else
        {
            // Dynamic friction
            frictionImpulse = -jN * mu * tangent;
        }

        rbA->setVelocity(rbA->getVelocity() - frictionImpulse * invMassA);
        rbB->setVelocity(rbB->getVelocity() + frictionImpulse * invMassB);
        rbA->setAngularVelocity(rbA->getAngularVelocity() - cross2D(rA, frictionImpulse) * invInertiaA);
        rbB->setAngularVelocity(rbB->getAngularVelocity() + cross2D(rB, frictionImpulse) * invInertiaB);
    }

    // --- Positional correction (prevent sinking) ---
    const float percent = 0.4f; // penetration percentage to correct
    const float slop = 0.01f;   // allowable penetration tolerance
    float penetration = collision->getPenetration();
    Eigen::Vector2f correction = std::max(penetration - slop, 0.0f) / (invMassA + invMassB) * percent * normal;
    if (rbA->getBodyType() != BodyType::STATIC)
    {
        rbA->getGameObject()->setPosition(
            rbA->getGameObject()->getPosition() - correction * invMassA);
    }
    if (rbB->getBodyType() != BodyType::STATIC)
    {
        rbB->getGameObject()->setPosition(
            rbB->getGameObject()->getPosition() + correction * invMassB);
    }

    markSupportContact(rbA, -normal);
    markSupportContact(rbB, normal);
}

void PhysicsManager::markSupportContact(Rigidbody *rigidBody, const Eigen::Vector2f &contactDirection)
{
    if (rigidBody == nullptr || rigidBody->getBodyType() != BodyType::DYNAMIC)
    {
        return;
    }

    float gravityNorm = gravityVector.norm();
    float contactNorm = contactDirection.norm();
    if (gravityNorm <= 0.0f || contactNorm <= 0.0f)
    {
        return;
    }

    Eigen::Vector2f supportDirection = -gravityVector / gravityNorm;
    Eigen::Vector2f normalizedContactDirection = contactDirection / contactNorm;

    if (normalizedContactDirection.dot(supportDirection) >= SUPPORT_NORMAL_THRESHOLD)
    {
        rigidBody->setIsSupportedThisFrame(true);
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
            if (!rigidBodyComponent->getIsSupportedThisFrame())
            {
                rigidBodyComponent->wakeUp();
            }
            continue;
        }

        if (!rigidBodyComponent->getIsSupportedThisFrame())
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