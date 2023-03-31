#include "circlecollider.h"

CircleCollider::CircleCollider(GameObject* gameObject) : Collider(gameObject, ComponentType::COLLIDER)
{
}

CircleCollider::~CircleCollider()
{
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