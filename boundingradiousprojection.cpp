#include "collider.h"

BoundingRadiusProjection::BoundingRadiusProjection()
{
}

BoundingRadiusProjection::BoundingRadiusProjection(Collider* collider, float projectedPosition, bool end)
    : collider(collider), projectedPosition(projectedPosition), end(end)
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

bool BoundingRadiusProjection::isEnd() const
{
    return end;
}

void BoundingRadiusProjection::setCollider(Collider* collider)
{
    this->collider = collider;
}

void BoundingRadiusProjection::setProjectedPosition(float projectedPosition)
{
    this->projectedPosition = projectedPosition;
}

void BoundingRadiusProjection::setEnd(bool end)
{
    this->end = end;
}

bool BoundingRadiusProjection::operator==(const BoundingRadiusProjection& other) const
{
    return projectedPosition == other.projectedPosition;
}

bool BoundingRadiusProjection::operator!=(const BoundingRadiusProjection& other) const
{
    return !(*this == other);
}

bool BoundingRadiusProjection::operator<(const BoundingRadiusProjection& other) const
{
    return projectedPosition < other.projectedPosition;
}

bool BoundingRadiusProjection::operator>(const BoundingRadiusProjection& other) const
{
    return projectedPosition > other.projectedPosition;
}

bool BoundingRadiusProjection::operator<=(const BoundingRadiusProjection& other) const
{
    return *this < other || *this == other;
}

bool BoundingRadiusProjection::operator>=(const BoundingRadiusProjection& other) const
{
    return *this > other || *this == other;
}
