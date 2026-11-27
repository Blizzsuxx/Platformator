#include "platformator/circlecollider.h"

#include "core/inputvalidation.h"
#include "core/objectpool.h"

PLATFORMATOR_DEFINE_POOLED_ALLOCATORS(CircleCollider, CircleCollider)

CircleCollider::CircleCollider()
    : Collider(ComponentType::COLLIDER, ColliderType::CircleCollider), radius(1.0f), publishedRadius(1.0f)
{
}

CircleCollider::CircleCollider(GameObject *gameObject, const float radius)
    : Collider(gameObject, ComponentType::COLLIDER, ColliderType::CircleCollider), radius(radius),
      publishedRadius(radius)
{
    platformator_detail::positive(radius, "circle.radius");
}

CircleCollider::~CircleCollider() = default;

Component *CircleCollider::clone() const
{
    auto cloned = std::make_unique<CircleCollider>();
    cloned->setRadius(radius);
    cloned->setOffset(offset);
    cloned->setIsTrigger(getIsTrigger());
    cloned->setCollisionGroup(collisionGroup);
    cloned->setCollisionMask(collisionMask);
    return cloned.release();
}

float CircleCollider::getRadius() const
{
    return radius * std::max(getGameObject()->getScale().x(), getGameObject()->getScale().y());
}

void CircleCollider::setRadius(const float radius)
{
    platformator_detail::positive(radius, "circle.radius");
    if (this->radius == radius)
        return;
    this->radius = radius;
    notifyShapeChanged();
}

void CircleCollider::updateCollider()
{
    publishedRadius = getRadius();
    const Eigen::Vector2f &position = getCachedWorldPosition();
    setTightBounds(position.x() - publishedRadius, position.x() + publishedRadius, position.y() - publishedRadius,
                   position.y() + publishedRadius);
}

float CircleCollider::getRadiusWithoutScale() const
{
    return radius;
}
