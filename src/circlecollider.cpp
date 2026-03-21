#include "circlecollider.h"

CircleCollider::CircleCollider(GameObject *gameObject, const float radius) : Collider(gameObject, ComponentType::COLLIDER), radius(radius)
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
    return radius * std::max(getGameObject()->getScale().x(), getGameObject()->getScale().y());
}

// Setters
void CircleCollider::setRadius(const float radius)
{
    this->radius = radius;
    updateCollider();
}

std::vector<Eigen::Vector2f> CircleCollider::getNormals(const Collider *other) const
{
    std::vector<Eigen::Vector2f> normals(1);

    normals[0] = other->getGameObject()->getPosition() - getGameObject()->getPosition();
    normals[0].normalize();

    return normals;
}

Eigen::Vector2f CircleCollider::projectOntoAxis(const Eigen::Vector2f &axis) const
{
    float value = axis.dot(getGameObject()->getPosition());
    float scaledRadius = getRadius();

    return Eigen::Vector2f(value - scaledRadius, value + scaledRadius);
}

void CircleCollider::generateProjections()
{
    float x = getGameObject()->getPosition().x();
    float y = getGameObject()->getPosition().y();
    float scaledRadius = getRadius();

    xProjections.getMin()->setProjectedPosition(x - scaledRadius);
    xProjections.getMax()->setProjectedPosition(x + scaledRadius);
    yProjections.getMin()->setProjectedPosition(y - scaledRadius);
    yProjections.getMax()->setProjectedPosition(y + scaledRadius);

    // TODO:
    // maybe check if the projections actually changed before setting dirty to true
    // this is only called when the position or the radius changes, so it is not that bad if we set dirty to true even if the projections did not change
    this->updateStateVersion();
}

void CircleCollider::updateCollider()
{
    generateProjections();
}