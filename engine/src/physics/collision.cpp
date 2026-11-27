#include "platformator/collision.h"

#include "core/constants.h"
#include "core/runtimeaccess.h"
#include "physics/physicsmanager.h"
#include "platformator/rigidbody.h"

Collision::Collision()
    : normal(), contactPoints(), referenceObject(nullptr), incidentObject(nullptr),
      supportState(SupportState::NONE_SUPPORT)
{
}

Collision::Collision(Collider *colliderA, Collider *colliderB)
    : normal(), contactPoints(), referenceObject(colliderA), incidentObject(colliderB),
      supportState(SupportState::NONE_SUPPORT)
{
}

const Eigen::Vector2f &Collision::getNormal() const
{
    return normal;
}

const ClipPointsWithData &Collision::getContactPoints() const
{
    return contactPoints;
}

ClipPointsWithData &Collision::getContactPoints()
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

void Collision::setNormal(const Eigen::Vector2f &normal)
{
    this->normal = normal;
}

void Collision::setContactPoints(const ClipPoints &newContactPoints)
{
    ClipPointsWithData contactPointsWithData;
    for (size_t i = 0; i < newContactPoints.count; ++i)
    {
        size_t index = contactPoints.findIndex(newContactPoints.points[i].feature);
        contactPointsWithData.points[i] = ClipPointWithData(newContactPoints.points[i]);
        if (index != SIZE_MAX)
        {
            contactPointsWithData.points[i].accumulatedNormalImpulse =
                contactPoints.points[index].accumulatedNormalImpulse;
            contactPointsWithData.points[i].accumulatedTangentImpulse =
                contactPoints.points[index].accumulatedTangentImpulse;
        }
    }
    contactPointsWithData.count = newContactPoints.count;
    this->contactPoints = contactPointsWithData;
}

void Collision::setReferenceObject(const Collider *colliderA)
{
    if (referenceObject != colliderA)
        contactPoints = ClipPointsWithData();
    this->referenceObject = colliderA;
}

void Collision::setIncidentObject(const Collider *colliderB)
{
    this->incidentObject = colliderB;
}

Collision::CollisionType Collision::getResolutionType() const
{
    if (referenceObject == nullptr || incidentObject == nullptr)
    {
        return CollisionType::NONE_COLLISIONS;
    }

    if (referenceObject->getIsTrigger() || incidentObject->getIsTrigger())
    {
        return CollisionType::TRIGGER;
    }

    const Rigidbody *rbA = referenceObject->getAttachedRigidbody();
    const Rigidbody *rbB = incidentObject->getAttachedRigidbody();
    if (rbA == nullptr || rbB == nullptr)
    {
        return CollisionType::NONE_COLLISIONS;
    }

    if (rbA->getBodyType() == BodyType::DYNAMIC || rbB->getBodyType() == BodyType::DYNAMIC)
    {
        return CollisionType::DYNAMIC;
    }
    if (rbA->getBodyType() == BodyType::KINEMATIC || rbB->getBodyType() == BodyType::KINEMATIC)
    {
        return CollisionType::KINEMATIC;
    }

    return CollisionType::NONE_COLLISIONS;
}

void Collision::updateSupportState(const Eigen::Vector2f &gravityVectorNormalized)
{
    supportState = NONE_SUPPORT;
    if (referenceObject->getIsTrigger() || incidentObject->getIsTrigger())
        return;
    const Rigidbody *a = referenceObject->getAttachedRigidbody();
    const Rigidbody *b = incidentObject->getAttachedRigidbody();
    if (!a || !b)
        return;
    if (a->qualifiesAsSupportContact(-normal, gravityVectorNormalized))
        supportState |= SUPPORTS_REFERENCE;
    if (b->qualifiesAsSupportContact(normal, gravityVectorNormalized))
        supportState |= SUPPORTS_INCIDENT;
}

void Collision::clearSupportState()
{
    supportState = NONE_SUPPORT;
}

bool Collision::isVerticalCollision() const
{
    const Eigen::Vector2f &gravityVectorNormalized =
        platformator_detail::RuntimeAccess::physicsManager()->getGravityVectorNormalized();
    return std::abs(normal.dot(-gravityVectorNormalized)) >= SUPPORT_NORMAL_THRESHOLD;
}

bool Collision::isHorizontalCollision() const
{
    return !isVerticalCollision();
}
