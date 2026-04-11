#include "collision.h"
#include "helpers.h"
#include "rigidbody.h"

Collision::Collision() : normal(), penetration(0.0f), contactPoints(), referenceObject(nullptr), incidentObject(nullptr)
{
}

Collision::Collision(Collider *colliderA, Collider *colliderB)
    : normal(), penetration(0.0f), contactPoints(), referenceObject(colliderA), incidentObject(colliderB)
{
}

Collision::Collision(const Eigen::Vector2f &normal, float penetration, const ClipPointsWithData &contactPoints, Collider *colliderA, Collider *colliderB)
    : normal(normal), penetration(penetration), contactPoints(contactPoints), referenceObject(colliderA), incidentObject(colliderB)
{
}

Collision::~Collision()
{
}

// Getters
const Eigen::Vector2f &Collision::getNormal() const
{
    return normal;
}

float Collision::getPenetration() const
{
    return penetration;
}

ClipPointsWithData &Collision::getContactPoints() const
{
    return contactPoints;
}

const Collider *Collision::getReferenceObject() const
{
    return referenceObject;
}

const Collider *Collision::getIncidentObject() const
{
    return incidentObject;
}

// Setters

void Collision::setNormal(const Eigen::Vector2f &normal) const
{
    this->normal = normal;
}

void Collision::setPenetration(const float penetration) const
{
    this->penetration = penetration;
}

void Collision::setContactPoints(const ClipPoints &newContactPoints) const
{
    ClipPointsWithData contactPointsWithData;
    for (size_t i = 0; i < newContactPoints.count; ++i)
    {
        size_t index = contactPoints.findIndex(newContactPoints.points[i]);
        if (index != -1)
        {
            contactPointsWithData.points[i].accumulatedNormalImpulse = contactPoints.points[index].accumulatedNormalImpulse;
            contactPointsWithData.points[i].accumulatedTangentImpulse = contactPoints.points[index].accumulatedTangentImpulse;
            contactPointsWithData.points[i].accumulatedNormalImpulseBias = contactPoints.points[index].accumulatedNormalImpulseBias;
        }
        else
        {
            contactPointsWithData.points[i] = ClipPointWithData(newContactPoints.points[i]);
        }
    }
    contactPointsWithData.count = newContactPoints.count;
    this->contactPoints = contactPointsWithData;
}

void Collision::setReferenceObject(const Collider *colliderA) const
{
    this->referenceObject = colliderA;
}

void Collision::setIncidentObject(const Collider *colliderB) const
{
    this->incidentObject = colliderB;
}

bool Collision::operator==(const Collision &other) const
{
    return (referenceObject == other.referenceObject && incidentObject == other.incidentObject) ||
           (referenceObject == other.incidentObject && incidentObject == other.referenceObject);
}

bool Collision::operator!=(const Collision &other) const
{
    return !(*this == other);
}

float Collision::getFriction() const
{
    Rigidbody *rbA = (Rigidbody *)referenceObject->getGameObject()->getComponent(ComponentType::RIGID_BODY);
    Rigidbody *rbB = (Rigidbody *)incidentObject->getGameObject()->getComponent(ComponentType::RIGID_BODY);

    if (rbA == nullptr || rbB == nullptr)
    {
        return 0.0f; // No friction if either object doesn't have a Rigidbody
    }

    float frictionA = rbA->getFriction();
    float frictionB = rbB->getFriction();

    return sqrt(frictionA * frictionB); // Using geometric mean for friction
}