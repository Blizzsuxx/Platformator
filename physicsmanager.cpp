#include "physicsmanager.h"

PhysicsManager::PhysicsManager()
    : rigidBodyComponents(), colliderComponents(), collisions(), aabb(), gravityVector(0.0f, -9.81f)
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
    rigidBodyComponents.push_back(rigidBodyComponent);
}

void PhysicsManager::addColliderComponent(Collider *colliderComponent)
{
    colliderComponents.push_back(colliderComponent);

    aabb.add(colliderComponent);
}

void PhysicsManager::removeRigidBodyComponent(Rigidbody *rigidBodyComponent)
{
    rigidBodyComponents.erase(std::remove(rigidBodyComponents.begin(), rigidBodyComponents.end(), rigidBodyComponent), rigidBodyComponents.end());
}

void PhysicsManager::removeColliderComponent(Collider *colliderComponent)
{
    colliderComponents.erase(std::remove(colliderComponents.begin(), colliderComponents.end(), colliderComponent), colliderComponents.end());

    aabb.remove(colliderComponent);
}

void PhysicsManager::broadPhase()
{
    aabb.sort();
    aabb.updateCandidateList();
}

void PhysicsManager::narrowPhase()
{
    collisions.clear();

    for (const Collision &collision : *aabb.getCandidateCollisions())
    {
        if (checkCollisions(&collision))
        {
            collisions.push_back(&collision);
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

bool PhysicsManager::checkCollisions(const Collision *collision)
{
    const Collider *referenceCollider = (const Collider *)collision->getReferenceObject()->getComponent(ComponentType::COLLIDER);
    const Collider *incidentCollider = (const Collider *)collision->getIncidentObject()->getComponent(ComponentType::COLLIDER);

    float minOverlap = std::numeric_limits<float>::max();
    Eigen::Vector2f minNormal;
    Eigen::Vector2f incidentProjection;
    const Collider *realIncidentCollider = incidentCollider;

    std::vector<Eigen::Vector2f> normals = referenceCollider->getNormals(incidentCollider);
    if (checkProjections(normals, referenceCollider, incidentCollider, minOverlap, minNormal, incidentProjection, realIncidentCollider) == false)
    {
        return false;
    }

    normals = incidentCollider->getNormals(referenceCollider);

    if (checkProjections(normals, incidentCollider, referenceCollider, minOverlap, minNormal, incidentProjection, realIncidentCollider) == false)
    {
        return false;
    }

    collision->setIncidentObject(realIncidentCollider->getGameObject());
    collision->setReferenceObject(realIncidentCollider == incidentCollider ? referenceCollider->getGameObject() : incidentCollider->getGameObject());

    // Ensure the normal points from reference toward incident
    auto directionVector = collision->getIncidentObject()->getPosition() - collision->getReferenceObject()->getPosition();
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

    return true;
}

void PhysicsManager::resolveCollisions()
{
}