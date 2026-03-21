#include "physicsmanager.h"
#include "debugdraw.h"

PhysicsManager::PhysicsManager()
    : rigidBodyComponents(), activeCollisions(), aabb(), gravityVector(0.0f, 9.81f)
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
        rigidBodyComponent->applyGravity(gravityVector);
        rigidBodyComponent->move(timeDelta);
    }
}

void PhysicsManager::checkForCollisions()
{
    broadPhase();
    narrowPhase();
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
    if (colliderComponent == nullptr || colliderComponent->getIsRegisteredInBroadPhase() || !colliderComponent->getGameObject()->getActive())
    {
        return;
    }

    aabb.add(colliderComponent);
    colliderComponent->setIsRegisteredInBroadPhase(true);
}

void PhysicsManager::refreshColliderComponent(Collider *colliderComponent)
{
    if (colliderComponent == nullptr)
    {
        return;
    }

    if (!colliderComponent->getGameObject()->getActive())
    {
        removeColliderComponent(colliderComponent);
        return;
    }

    if (colliderComponent->getIsRegisteredInBroadPhase())
    {
        aabb.remove(colliderComponent);
        colliderComponent->setIsRegisteredInBroadPhase(false);
    }

    aabb.add(colliderComponent);
    colliderComponent->setIsRegisteredInBroadPhase(true);
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
    if (colliderComponent == nullptr || !colliderComponent->getIsRegisteredInBroadPhase())
    {
        return;
    }

    aabb.remove(colliderComponent);
    colliderComponent->setIsRegisteredInBroadPhase(false);
}

void PhysicsManager::broadPhase()
{
    aabb.sort();
}

void PhysicsManager::narrowPhase()
{
    activeCollisions.clear();

    for (const ColliderPair &pair : *aabb.getCandidatePairSet())
    {
        Collider *objectA = pair.getObjectA();
        Collider *objectB = pair.getObjectB();

        if (objectA == nullptr || objectB == nullptr || objectA->getGameObject()->getIsMarkedForDeletion() || objectB->getGameObject()->getIsMarkedForDeletion())
        {
            continue;
        }
        if (!objectA->getGameObject()->getActive() || !objectB->getGameObject()->getActive())
        {
            continue;
        }

        if (!pair.shouldUpdate())
        {
            pair.triggerCollisionStay();
        }
        else
        {
            satCreateCollision(pair);
        }

        if (pair.getCollision() != nullptr)
        {
            activeCollisions.push_back(pair.getCollision());
            DebugDraw::getInstance().addCollisionDebugObject(*pair.getCollision());
        }
    }
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

void PhysicsManager::resolveCollisions()
{
    for (Collision *collision : activeCollisions)
    {
        const Collider *referenceCollider = collision->getReferenceObject();
        const Collider *incidentCollider = collision->getIncidentObject();

        if (referenceCollider == nullptr || incidentCollider == nullptr || referenceCollider->getGameObject()->getIsMarkedForDeletion() || incidentCollider->getGameObject()->getIsMarkedForDeletion())
        {
            continue;
        }

        if (!referenceCollider->getIsTrigger() && !incidentCollider->getIsTrigger())
        {
            resolveCollision(collision);
        }
    }
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
}