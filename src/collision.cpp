#include "collision.h"
#include "helpers.h"

Collision::Collision() : normal(), penetration(0.0f), contactPoints(), referenceObject(nullptr), incidentObject(nullptr)
{
}

Collision::Collision(Collider *colliderA, Collider *colliderB)
    : normal(), penetration(0.0f), contactPoints(), referenceObject(colliderA), incidentObject(colliderB)
{
}

Collision::Collision(const Eigen::Vector2f &normal, float penetration, const ClipPoints &contactPoints, Collider *colliderA, Collider *colliderB)
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

const ClipPoints &Collision::getContactPoints() const
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

void Collision::setContactPoints(const ClipPoints &contactPoints) const
{
    this->contactPoints = contactPoints;
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
