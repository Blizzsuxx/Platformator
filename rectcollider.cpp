#include "rectcollider.h"

RectCollider::RectCollider(GameObject* gameObject) : Collider(gameObject, ComponentType::COLLIDER)
{
}

RectCollider::~RectCollider()
{
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