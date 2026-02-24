#include "collision.h"

Collision::Collision() : normal(), penetration(0.0f), contactPoint(), referenceObject(nullptr), incidentObject(nullptr)
{
}

Collision::Collision(GameObject *gameObjectA, GameObject *gameObjectB)
    : normal(), penetration(0.0f), contactPoint(), referenceObject(gameObjectA), incidentObject(gameObjectB)
{
}

Collision::Collision(const Eigen::Vector2f &normal, float penetration, const Eigen::Vector2f &contactPoint, GameObject *gameObjectA, GameObject *gameObjectB)
    : normal(normal), penetration(penetration), contactPoint(contactPoint), referenceObject(gameObjectA), incidentObject(gameObjectB)
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

const GameObject *Collision::getReferenceObject() const
{
    return referenceObject;
}

const GameObject *Collision::getIncidentObject() const
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

void Collision::setReferenceObject(GameObject *gameObjectA) const
{
    this->referenceObject = gameObjectA;
}

void Collision::setIncidentObject(GameObject *gameObjectB) const
{
    this->incidentObject = gameObjectB;
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