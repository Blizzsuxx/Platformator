#include "rectcollider.h"

RectCollider::RectCollider(GameObject* gameObject) : Collider(gameObject, ComponentType::COLLIDER)
{
}

RectCollider::RectCollider(GameObject* gameObject, const float width, const float height) 
    : Collider(gameObject, ComponentType::COLLIDER), width(width), height(height)
{
    boundingRadius = sqrt(pow(width, 2) + pow(height, 2)) / 2;
}

RectCollider::~RectCollider()
{
}

// Inherited via Collider
float RectCollider::getBoundingRadius() const
{
    return boundingRadius;
}

// Getters
float RectCollider::getWidth() const
{
    return width;
}

float RectCollider::getHeight() const
{
    return height;
}

// Setters
void RectCollider::setWidth(const float width)
{
    this->width = width;
}

void RectCollider::setHeight(const float height)
{
    this->height = height;
}