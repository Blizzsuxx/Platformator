#include "collider.h"

Collider::Collider(GameObject* gameObject, ComponentType type)
    : Component(gameObject, type), layer(0), isTrigger(false), projections()
{
}

Collider::~Collider()
{
    
}

BoundingRadiusProjection& Collider::getProjection(const int index)
{
    return projections[index];
}

BoundingRadiusProjection* Collider::getProjections()
{
    return projections;
}

int Collider::getLayer() const
{
    return layer;
}

void Collider::setLayer(const int layer)
{
    this->layer = layer;
}

bool Collider::isTriggered() const
{
    return isTrigger;
}

void Collider::setTrigger(const bool isTrigger)
{
    this->isTrigger = isTrigger;
}
