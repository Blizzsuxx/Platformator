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
    return radius;
}

// Setters
void CircleCollider::setRadius(const float radius)
{
    this->radius = radius;
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

    return Eigen::Vector2f(value - radius, value + radius);
}

void CircleCollider::generateProjections()
{
    float x = getGameObject()->getPosition().x();
    float y = getGameObject()->getPosition().y();

    xProjections.getMin()->setProjectedPosition(x - radius);
    xProjections.getMax()->setProjectedPosition(x + radius);
    yProjections.getMin()->setProjectedPosition(y - radius);
    yProjections.getMax()->setProjectedPosition(y + radius);

    // TODO:
    // maybe check if the projections actually changed before setting dirty to true
    // this is only called when the position or the radius changes, so it is not that bad if we set dirty to true even if the projections did not change
    this->updateStateVersion();
}

void CircleCollider::updateCollider()
{
    generateProjections();
}