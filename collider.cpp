#include "collider.h"

Collider::Collider(GameObject *gameObject, ComponentType type)
    : Component(gameObject, type), layer(0), isTrigger(false), isDirty(false), xProjections(this, 0.0f, 0.0f), yProjections(this, 0.0f, 0.0f)
{
}

Collider::~Collider()
{
}

BoundingRadiusProjectionAxis *Collider::getXProjections()
{
    return &xProjections;
}

BoundingRadiusProjectionAxis *Collider::getYProjections()
{
    return &yProjections;
}

int Collider::getLayer() const
{
    return layer;
}

void Collider::setLayer(const int layer)
{
    this->layer = layer;
}

bool Collider::getIsTrigger() const
{
    return isTrigger;
}

void Collider::setIsTrigger(const bool isTrigger)
{
    this->isTrigger = isTrigger;
}

bool Collider::getIsDirty() const
{
    return isDirty;
}

void Collider::setIsDirty(const bool isDirty)
{
    this->isDirty = isDirty;
}
