#include "physicsmanager.h"

PhysicsManager::PhysicsManager()
    : rigidBodyComponents(), colliderComponents(), collisions(), aabb(), gravityVector(0.0f, -9.81f), timeDelta(0.0f), lastUpdateTime(0.0f)
{
    lastUpdateTime = static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()) / 1000.0;
}

PhysicsManager::~PhysicsManager()
{
}

void PhysicsManager::applyPhysics()
{
    double currentTime = static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()) / 1000.0;
    timeDelta = currentTime - lastUpdateTime;
    lastUpdateTime = currentTime;

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
    rigidBodyComponents.push_back(rigidBodyComponent);
}

void PhysicsManager::addColliderComponent(Collider *colliderComponent)
{
    colliderComponents.push_back(colliderComponent);

    aabb.add(colliderComponent);
}

void PhysicsManager::broadPhase()
{
    aabb.updateCandidateList();
}

void PhysicsManager::narrowPhase()
{
    collisions.clear();

    for (Collision &collision : *aabb.getCandidateCollisions())
    {
        if (checkCollisions(&collision))
        {
            collisions.push_back(&collision);
        }
    }
}

bool PhysicsManager::checkProjections(std::span<const Eigen::Vector2f> &normals, Collider *referenceCollider, Collider *incidentCollider, float &minOverlap, Eigen::Vector2f &minNormal, Eigen::Vector2f &incidentProjection, Collider *&realIncidentCollider)
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

bool PhysicsManager::checkCollisions(Collision *collision)
{
    Collider *referenceCollider = (Collider *)collision->getReferenceObject()->getComponent(ComponentType::COLLIDER);
    Collider *incidentCollider = (Collider *)collision->getIncidentObject()->getComponent(ComponentType::COLLIDER);

    float minOverlap = std::numeric_limits<float>::max();
    Eigen::Vector2f minNormal;
    Eigen::Vector2f incidentProjection;
    Collider *realIncidentCollider = incidentCollider;

    std::span<const Eigen::Vector2f> normals = referenceCollider->getNormals(incidentCollider);
    if (checkProjections(normals, referenceCollider, incidentCollider, minOverlap, minNormal, incidentProjection, realIncidentCollider) == false)
    {
        return false;
    }

    normals = incidentCollider->getNormals(referenceCollider);

    if (checkProjections(normals, incidentCollider, referenceCollider, minOverlap, minNormal, incidentProjection, realIncidentCollider) == false)
    {
        return false;
    }

    collision->setNormal(minNormal);
    collision->setPenetration(minOverlap);
    collision->setIncidentObject(realIncidentCollider->getGameObject());
    collision->setReferenceObject(realIncidentCollider == incidentCollider ? referenceCollider->getGameObject() : incidentCollider->getGameObject());

    auto directionVector = collision->getIncidentObject()->getPosition() - collision->getReferenceObject()->getPosition();
    if (minNormal.dot(directionVector) < 0)
    {
        minNormal *= -1.0f;
    }

    float projectionDelta = (incidentProjection.y() - incidentProjection.x()) / 2;
    collision->setContactPoint(Eigen::Vector2f((realIncidentCollider->getGameObject()->getPosition() + (minNormal * projectionDelta))));

    return true;
}

void PhysicsManager::resolveCollisions()
{
}