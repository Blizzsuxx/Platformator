#include "collision.h"
#include "constants.h"
#include "helpers.h"
#include "rigidbody.h"

Collision::Collision() : normal(), contactPoints(), referenceObject(nullptr), incidentObject(nullptr), supportsReferenceBody(false), supportsIncidentBody(false)
{
}

Collision::Collision(Collider *colliderA, Collider *colliderB)
    : normal(), contactPoints(), referenceObject(colliderA), incidentObject(colliderB), supportsReferenceBody(false), supportsIncidentBody(false)
{
}

Collision::Collision(const Eigen::Vector2f &normal, float penetration, const ClipPointsWithData &contactPoints, Collider *colliderA, Collider *colliderB)
    : normal(normal), contactPoints(contactPoints), referenceObject(colliderA), incidentObject(colliderB), supportsReferenceBody(false), supportsIncidentBody(false)
{
}

Collision::~Collision()
{
    clearSupportState();
}

// Getters
const Eigen::Vector2f &Collision::getNormal() const
{
    return normal;
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

void Collision::setContactPoints(const ClipPoints &newContactPoints) const
{
    ClipPointsWithData contactPointsWithData;
    for (size_t i = 0; i < newContactPoints.count; ++i)
    {
        size_t index = contactPoints.findIndex(newContactPoints.points[i]);
        if (index != SIZE_MAX)
        {
            contactPointsWithData.points[i].accumulatedNormalImpulse = contactPoints.points[index].accumulatedNormalImpulse;
            contactPointsWithData.points[i].accumulatedTangentImpulse = contactPoints.points[index].accumulatedTangentImpulse;
            contactPointsWithData.points[i].accumulatedNormalImpulseBias = contactPoints.points[index].accumulatedNormalImpulseBias;
            contactPointsWithData.points[i].point = newContactPoints.points[i];
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

float Collision::getRestitution() const
{
    Rigidbody *rbA = (Rigidbody *)referenceObject->getGameObject()->getComponent(ComponentType::RIGID_BODY);
    Rigidbody *rbB = (Rigidbody *)incidentObject->getGameObject()->getComponent(ComponentType::RIGID_BODY);

    if (rbA == nullptr || rbB == nullptr)
    {
        return 0.0f;
    }

    return std::max(rbA->getRestitution(), rbB->getRestitution());
}

bool Collision::shouldResolve() const
{
    if (referenceObject == nullptr || incidentObject == nullptr)
    {
        return false;
    }

    if (referenceObject->getIsTrigger() || incidentObject->getIsTrigger())
    {
        return false; // Don't resolve collisions involving triggers
    }

    Rigidbody *rbA = (Rigidbody *)referenceObject->getGameObject()->getComponent(ComponentType::RIGID_BODY);
    Rigidbody *rbB = (Rigidbody *)incidentObject->getGameObject()->getComponent(ComponentType::RIGID_BODY);

    if (rbA == nullptr || rbB == nullptr)
    {
        return false; // Don't resolve collisions if either object doesn't have a Rigidbody
    }

    if (rbA->getBodyType() == BodyType::STATIC && rbB->getBodyType() == BodyType::STATIC)
    {
        return false; // Don't resolve collisions between two static objects
    }

    return true;
}

void Collision::updateSupportState(const Eigen::Vector2f &gravityVectorNormalized) const
{
    Rigidbody *rbA = (Rigidbody *)referenceObject->getGameObject()->getComponent(ComponentType::RIGID_BODY);
    Rigidbody *rbB = (Rigidbody *)incidentObject->getGameObject()->getComponent(ComponentType::RIGID_BODY);

    if (rbA == nullptr || rbB == nullptr)
    {
        return; // No support state to update if either object doesn't have a Rigidbody
    }

    bool newSupportsReferenceBody = rbA->qualifiesAsSupportContact(-normal, gravityVectorNormalized);
    bool newSupportsIncidentBody = rbB->qualifiesAsSupportContact(normal, gravityVectorNormalized);

    if (supportsReferenceBody != newSupportsReferenceBody)
    {
        if (rbA != nullptr)
        {
            if (newSupportsReferenceBody)
            {
                rbA->addSupportContact();
            }
            else
            {
                rbA->removeSupportContact();
            }
        }
        supportsReferenceBody = newSupportsReferenceBody;
    }

    if (supportsIncidentBody != newSupportsIncidentBody)
    {
        if (rbB != nullptr)
        {
            if (newSupportsIncidentBody)
            {
                rbB->addSupportContact();
            }
            else
            {
                rbB->removeSupportContact();
            }
        }
        supportsIncidentBody = newSupportsIncidentBody;
    }
}

void Collision::clearSupportState() const
{
    if (supportsReferenceBody && referenceObject != nullptr)
    {
        Rigidbody *rbA = (Rigidbody *)referenceObject->getGameObject()->getComponent(ComponentType::RIGID_BODY);
        if (rbA != nullptr)
        {
            rbA->removeSupportContact();
        }
        supportsReferenceBody = false;
    }

    if (supportsIncidentBody && incidentObject != nullptr)
    {
        Rigidbody *rbB = (Rigidbody *)incidentObject->getGameObject()->getComponent(ComponentType::RIGID_BODY);
        if (rbB != nullptr)
        {
            rbB->removeSupportContact();
        }
        supportsIncidentBody = false;
    }
}