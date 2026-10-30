#include "platformator/circlecollider.h"
#include "helpers.h"
#include "platformator/rigidbody.h"
#include "inputvalidation.h"

CircleCollider::CircleCollider() : Collider(ComponentType::COLLIDER, ColliderType::CircleCollider), radius(1.0f)
{
}

CircleCollider::CircleCollider(GameObject *gameObject, const float radius)
    : Collider(gameObject, ComponentType::COLLIDER, ColliderType::CircleCollider), radius(radius)
{
    platformator_detail::positive(radius, "circle.radius");
}

CircleCollider::~CircleCollider()
{
}

// Getters
float CircleCollider::getRadius() const
{
    return radius * std::max(getGameObject()->getScale().x(), getGameObject()->getScale().y());
}

// Setters
void CircleCollider::setRadius(const float radius)
{
    platformator_detail::positive(radius, "circle.radius");
    if (this->radius == radius)
        return;
    this->radius = radius;

    GameObject *gameObject = getGameObject();
    if (gameObject == nullptr)
    {
        return;
    }

    Rigidbody *rigidbodyComponent = gameObject->getComponent<Rigidbody>();
    if (rigidbodyComponent != nullptr)
    {
        rigidbodyComponent->refreshMomentOfInertiaCache();
    }

    scheduleSync();
}

std::vector<Eigen::Vector2f> CircleCollider::getNormals(const Collider *other) const
{
    std::vector<Eigen::Vector2f> normals(1);

    normals[0] = other->getCachedWorldPosition() - getCachedWorldPosition();

    if (normals[0].squaredNorm() <= 1e-12f)
    {
        normals[0] = Eigen::Vector2f(1.0f, 0.0f);
    }
    else
    {
        normals[0].normalize();
    }

    return normals;
}

Eigen::Vector2f CircleCollider::projectOntoAxis(const Eigen::Vector2f &axis) const
{
    float value = axis.dot(getCachedWorldPosition());
    float scaledRadius = getRadius();

    return Eigen::Vector2f(value - scaledRadius, value + scaledRadius);
}

void CircleCollider::generateProjections()
{
    Eigen::Vector2f position = getCachedWorldPosition();
    float scaledRadius = getRadius();

    xProjections.getMin()->setProjectedPosition(position.x() - scaledRadius);
    // repairMinProjectionProxiesForProjection(xProjections.getMin(), &xProjections);

    xProjections.getMax()->setProjectedPosition(position.x() + scaledRadius);
    // repairMaxProjectionProxiesForProjection(xProjections.getMax(), &xProjections);

    yProjections.getMin()->setProjectedPosition(position.y() - scaledRadius);
    // repairMinProjectionProxiesForProjection(yProjections.getMin(), &yProjections);

    yProjections.getMax()->setProjectedPosition(position.y() + scaledRadius);
    // repairMaxProjectionProxiesForProjection(yProjections.getMax(), &yProjections);
}

void CircleCollider::updateCollider()
{
    generateProjections();
}

Edge CircleCollider::getEdgeWithNormal(const Eigen::Vector2f &normal) const
{
    Eigen::Vector2f direction = normal;
    if (direction.squaredNorm() <= 1e-12f)
    {
        direction = Eigen::Vector2f(1.0f, 0.0f);
    }
    else
    {
        direction.normalize();
    }

    Eigen::Vector2f supportPoint = getCachedWorldPosition() + direction * getRadius();
    return Edge(supportPoint, supportPoint, supportPoint, NO_EDGE);
}

float CircleCollider::getRadiusWithoutScale() const
{
    return radius;
}
