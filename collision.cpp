#include "collision.h"

Collision::Collision()
{
}

Collision::Collision(GameObject *gameObjectA, GameObject *gameObjectB)
    : referenceObject(gameObjectA), incidentObject(gameObjectB)
{
}

Collision::Collision(Eigen::Vector2f *normal, float penetration, Eigen::Vector2f *contactPoint, GameObject *gameObjectA, GameObject *gameObjectB)
    : normal(normal), penetration(penetration), contactPoint(contactPoint), referenceObject(gameObjectA), incidentObject(gameObjectB)
{
}

Collision::~Collision()
{
    if (normal != nullptr)
    {
        delete normal;
    }

    if (contactPoint != nullptr)
    {
        delete contactPoint;
    }
}

// Getters
const Eigen::Vector2f *Collision::getNormal() const
{
    return normal;
}

float Collision::getPenetration() const
{
    return penetration;
}

const Eigen::Vector2f *Collision::getContactPoint() const
{
    return contactPoint;
}

GameObject *Collision::getReferenceObject()
{
    return referenceObject;
}

GameObject *Collision::getIncidentObject()
{
    return incidentObject;
}

// Setters

void Collision::setNormal(Eigen::Vector2f *normal)
{
    this->normal = normal;
}

void Collision::setPenetration(const float penetration)
{
    this->penetration = penetration;
}

void Collision::setContactPoint(Eigen::Vector2f *contactPoint)
{
    this->contactPoint = contactPoint;
}

void Collision::setReferenceObject(GameObject *gameObjectA)
{
    this->referenceObject = gameObjectA;
}

void Collision::setIncidentObject(GameObject *gameObjectB)
{
    this->incidentObject = gameObjectB;
}
