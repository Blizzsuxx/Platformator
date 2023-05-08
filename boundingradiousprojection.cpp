#include "boundingradiousprojection.h"

BoundingRadiusProjection::BoundingRadiusProjection()
{
}

BoundingRadiusProjection::BoundingRadiusProjection(Collider* collider, float projectedPosition)
    : collider(collider), projectedPosition(projectedPosition)
{
}

BoundingRadiusProjection::~BoundingRadiusProjection()
{
}

Collider* BoundingRadiusProjection::getCollider()
{
    return collider;
}

float BoundingRadiusProjection::getProjectedPosition() const
{
    return projectedPosition;
}

void BoundingRadiusProjection::setCollider(Collider* collider)
{
    this->collider = collider;
}

void BoundingRadiusProjection::setProjectedPosition(float projectedPosition)
{
    this->projectedPosition = projectedPosition;
}

bool BoundingRadiusProjection::operator==(const BoundingRadiusProjection& other) const
{
    return projectedPosition == other.projectedPosition && collider->getBoundingRadius() == other.collider->getBoundingRadius();
}

bool BoundingRadiusProjection::operator!=(const BoundingRadiusProjection& other) const
{
    return !(*this == other);
}

bool BoundingRadiusProjection::operator<(const BoundingRadiusProjection& other) const
{
    return projectedPosition + collider->getBoundingRadius() < other.projectedPosition + other.collider->getBoundingRadius();
}

bool BoundingRadiusProjection::operator>(const BoundingRadiusProjection& other) const
{
    return projectedPosition + collider->getBoundingRadius() > other.projectedPosition + other.collider->getBoundingRadius();
}

bool BoundingRadiusProjection::operator<=(const BoundingRadiusProjection& other) const
{
    return *this < other || *this == other;
}

bool BoundingRadiusProjection::operator>=(const BoundingRadiusProjection& other) const
{
    return *this > other || *this == other;
}
