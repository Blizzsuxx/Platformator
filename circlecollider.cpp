#include "circlecollider.h"

CircleCollider::CircleCollider(GameObject *gameObject) : Collider(gameObject, ComponentType::COLLIDER), radius(0.0f)
{
}

CircleCollider::~CircleCollider()
{
}

ColliderType CircleCollider::getColliderType() const
{
    return ColliderType::CircleCollider;
}

// Getters
float CircleCollider::getRadius() const
{
    return radius;
}

// Setters
void CircleCollider::setRadius(const float radius)
{
    this->radius = radius;
}

std::span<const Eigen::Vector2f> CircleCollider::getNormals(Collider *other)
{
    std::array<Eigen::Vector2f, 1> normals;

    normals[0] = other->getGameObject()->getPosition() - getGameObject()->getPosition();
    normals[0].normalize();

    return normals;
}

Eigen::Vector2f CircleCollider::projectOntoAxis(const Eigen::Vector2f &axis)
{
    float value = axis.dot(getGameObject()->getPosition());

    return Eigen::Vector2f(value - radius, value + radius);
}

void CircleCollider::generateProjections()
{
    float x = getGameObject()->getPosition().x();
    float y = getGameObject()->getPosition().y();

    xProjections = BoundingRadiusProjectionAxis(this, x - radius, x + radius);
    yProjections = BoundingRadiusProjectionAxis(this, y - radius, y + radius);
}

void CircleCollider::updateCollider()
{
    generateProjections();
}