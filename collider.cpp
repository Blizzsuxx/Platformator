#include "collider.h"

Collider::Collider(GameObject* gameObject, ComponentType type) : Component(gameObject, type)
{
}

Collider::~Collider()
{
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
