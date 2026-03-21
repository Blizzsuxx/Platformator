#include "collision.h"

Collision::Collision() : normal(), penetration(0.0f), contactPoint(), referenceObject(nullptr), incidentObject(nullptr)
{
}

Collision::Collision(Collider *colliderA, Collider *colliderB)
    : normal(), penetration(0.0f), contactPoint(), referenceObject(colliderA), incidentObject(colliderB)
{
}

Collision::Collision(const Eigen::Vector2f &normal, float penetration, const Eigen::Vector2f &contactPoint, Collider *colliderA, Collider *colliderB)
    : normal(normal), penetration(penetration), contactPoint(contactPoint), referenceObject(colliderA), incidentObject(colliderB)
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

const Eigen::Vector2f &Collision::getContactPoint() const
{
    return contactPoint;
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

void Collision::setContactPoint(const Eigen::Vector2f &contactPoint) const
{
    this->contactPoint = contactPoint;
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
