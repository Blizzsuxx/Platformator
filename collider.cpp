#include "collider.h"

Collider::Collider(GameObject *gameObject, ComponentType type)
    : Component(gameObject, type), layer(0), isTrigger(false), xProjections(nullptr, 0, 0), yProjections(nullptr, 0, 0)
{
    generateProjections();
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

void Collider::generateProjections()
{
    float x = getGameObject()->getPosition().x();
    float y = getGameObject()->getPosition().y();
    float radius = getBoundingBoxLengthX();

    xProjections = BoundingRadiusProjectionAxis(this, x - radius, x + radius);
    yProjections = BoundingRadiusProjectionAxis(this, y - radius, y + radius);
}