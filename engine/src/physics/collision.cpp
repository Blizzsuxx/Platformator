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
    const size_t oldCount = contactPoints.count;
    const uint32_t oldKey0 = oldCount > 0 ? contactPoints.points[0].point.feature.key : 0;
    const float oldN0 = oldCount > 0 ? contactPoints.points[0].accumulatedNormalImpulse : 0.0f;
    const float oldT0 = oldCount > 0 ? contactPoints.points[0].accumulatedTangentImpulse : 0.0f;
    const uint32_t oldKey1 = oldCount > 1 ? contactPoints.points[1].point.feature.key : 0;
    const float oldN1 = oldCount > 1 ? contactPoints.points[1].accumulatedNormalImpulse : 0.0f;
    const float oldT1 = oldCount > 1 ? contactPoints.points[1].accumulatedTangentImpulse : 0.0f;

    for (size_t i = 0; i < newContactPoints.count; ++i)
    {
        ClipPointWithData &dst = contactPoints.points[i];
        dst.point = newContactPoints.points[i];
        dst.separation = 0.0f;
        const uint32_t key = dst.point.feature.key;
        if (oldCount > 0 && key == oldKey0)
        {
            dst.accumulatedNormalImpulse = oldN0;
            dst.accumulatedTangentImpulse = oldT0;
        }
        else if (oldCount > 1 && key == oldKey1)
        {
            dst.accumulatedNormalImpulse = oldN1;
            dst.accumulatedTangentImpulse = oldT1;
        }
        else
        {
            dst.accumulatedNormalImpulse = 0.0f;
            dst.accumulatedTangentImpulse = 0.0f;
        }
    }
    contactPoints.count = newContactPoints.count;
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
