#include "collider.h"

Collider::Collider(GameObject *gameObject, ComponentType type)
    : Component(gameObject, type), layer(0), isTrigger(false), projections()
{
}

Collider::~Collider()
{
}

BoundingRadiusProjection &Collider::getProjection(const size_t index)
{
    return projections[index];
}

BoundingRadiusProjection *Collider::getProjections()
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

void Collider::generateProjections()
{
    float x = getGameObject()->getPosition().x();
    float y = getGameObject()->getPosition().y();
    float radius = getBoundingBoxLengthX();
    projections[0] = BoundingRadiusProjection(this, x - radius, false);
    projections[1] = BoundingRadiusProjection(this, x + radius, true);
    projections[2] = BoundingRadiusProjection(this, y - radius, false);
    projections[3] = BoundingRadiusProjection(this, y + radius, true);
}