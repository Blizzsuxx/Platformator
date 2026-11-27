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
    const bool refNonStatic = referenceObject->hasNonStaticRigidbody();
    const bool incNonStatic = incidentObject->hasNonStaticRigidbody();
    if (!refNonStatic && !incNonStatic)
        return;
    // Both endpoints must have a Rigidbody (checked lazily only if the other is static).
    if ((!refNonStatic && referenceObject->getAttachedRigidbody() == nullptr) ||
        (!incNonStatic && incidentObject->getAttachedRigidbody() == nullptr))
        return;
    const float proj = normal.dot(gravityVectorNormalized);
    if (refNonStatic && proj >= SUPPORT_NORMAL_THRESHOLD)
        supportState |= SUPPORTS_REFERENCE;
    if (incNonStatic && -proj >= SUPPORT_NORMAL_THRESHOLD)
        supportState |= SUPPORTS_INCIDENT;
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
