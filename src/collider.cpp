#include "collider.h"
#include "localsortarray.h"

Collider::Collider(GameObject *gameObject, ComponentType type)
    : Component(gameObject, type), layer(0), isTrigger(false), isDirty(true), xProjections(this, 0.0f, 0.0f), yProjections(this, 0.0f, 0.0f)
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
    this->getXProjections()->getMax()->getChunk()->setIsDirty(isDirty);
    this->getXProjections()->getMin()->getChunk()->setIsDirty(isDirty);

    this->getYProjections()->getMax()->getChunk()->setIsDirty(isDirty);
    this->getYProjections()->getMin()->getChunk()->setIsDirty(isDirty);
}

void Collider::triggerCollisionEnter(const Collider *other) const
{
    // Placeholder for collision enter event handling
    // In a full implementation, this would notify the game object or other systems of the collision event
}

void Collider::triggerCollisionExit(const Collider *other) const
{
    // Placeholder for collision exit event handling
    // In a full implementation, this would notify the game object or other systems of the collision event
}