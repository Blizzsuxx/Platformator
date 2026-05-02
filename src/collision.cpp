#include "collision.h"
#include "constants.h"
#include "helpers.h"
#include "rigidbody.h"
#include "gamemanager.h"

Collision::Collision() : normal(), contactPoints(), referenceObject(nullptr), incidentObject(nullptr), supportState(SupportState::NONE_SUPPORT)
{
}

Collision::Collision(Collider *colliderA, Collider *colliderB)
    : normal(), contactPoints(), referenceObject(colliderA), incidentObject(colliderB), supportState(SupportState::NONE_SUPPORT)
{
}

Collision::Collision(const Eigen::Vector2f &normal, float penetration, const ClipPointsWithData &contactPoints, Collider *colliderA, Collider *colliderB)
    : normal(normal), contactPoints(contactPoints), referenceObject(colliderA), incidentObject(colliderB), supportState(SupportState::NONE_SUPPORT)
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

Collision::CollisionType Collision::getResolutionType() const
{
    if (referenceObject == nullptr || incidentObject == nullptr)
    {
        return CollisionType::NONE_COLLISIONS;
    }

    if (referenceObject->getIsTrigger() || incidentObject->getIsTrigger())
    {
        return CollisionType::TRIGGER; // Don't resolve collisions involving triggers
    }

    Rigidbody *rbA = (Rigidbody *)referenceObject->getGameObject()->getComponent(ComponentType::RIGID_BODY);
    Rigidbody *rbB = (Rigidbody *)incidentObject->getGameObject()->getComponent(ComponentType::RIGID_BODY);

    if (rbA == nullptr || rbB == nullptr)
    {
        return CollisionType::NONE_COLLISIONS; // Don't resolve collisions if either object doesn't have a Rigidbody
    }

    if (rbA->getBodyType() == BodyType::DYNAMIC || rbB->getBodyType() == BodyType::DYNAMIC)
    {
        return CollisionType::DYNAMIC;
    }
    else if (rbA->getBodyType() == BodyType::KINEMATIC || rbB->getBodyType() == BodyType::KINEMATIC)
    {
        return CollisionType::KINEMATIC;
    }

    return CollisionType::NONE_COLLISIONS;
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

    bool supportsReferenceBody = getSupportsReferenceBody();
    bool supportsIncidentBody = getSupportsIncidentBody();

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
        setSupportsReferenceBody(newSupportsReferenceBody);
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
        setSupportsIncidentBody(newSupportsIncidentBody);
    }
}

void Collision::clearSupportState() const
{
    bool supportsReferenceBody = getSupportsReferenceBody();
    bool supportsIncidentBody = getSupportsIncidentBody();

    if (supportsReferenceBody && referenceObject != nullptr)
    {
        Rigidbody *rbA = (Rigidbody *)referenceObject->getGameObject()->getComponent(ComponentType::RIGID_BODY);
        if (rbA != nullptr)
        {
            rbA->removeSupportContact();
        }
        setSupportsReferenceBody(false);
    }

    if (supportsIncidentBody && incidentObject != nullptr)
    {
        Rigidbody *rbB = (Rigidbody *)incidentObject->getGameObject()->getComponent(ComponentType::RIGID_BODY);
        if (rbB != nullptr)
        {
            rbB->removeSupportContact();
        }
        setSupportsIncidentBody(false);
    }
}

bool Collision::getSupportsReferenceBody() const
{
    return supportState & SUPPORTS_REFERENCE;
}

bool Collision::getSupportsIncidentBody() const
{
    return supportState & SUPPORTS_INCIDENT;
}

void Collision::setSupportsReferenceBody(bool supports) const
{
    if (supports)
    {
        supportState |= SUPPORTS_REFERENCE;
    }
    else
    {
        supportState &= ~SUPPORTS_REFERENCE;
    }
}

void Collision::setSupportsIncidentBody(bool supports) const
{
    if (supports)
    {
        supportState |= SUPPORTS_INCIDENT;
    }
    else
    {
        supportState &= ~SUPPORTS_INCIDENT;
    }
}

bool Collision::isVerticalCollision() const
{
    const Eigen::Vector2f &gravityVectorNormalized = GameManager::getInstance().getGravityVectorNormalized();
    return normal.dot(-gravityVectorNormalized) >= SUPPORT_NORMAL_THRESHOLD;
}

bool Collision::isHorizontalCollision() const
{
    return !isVerticalCollision();
}