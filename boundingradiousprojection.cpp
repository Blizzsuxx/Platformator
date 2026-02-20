#include "collider.h"

BoundingRadiusProjection::BoundingRadiusProjection() : collider(nullptr), projectedPosition(0.0f), isEnd(false)
{
}

BoundingRadiusProjection::BoundingRadiusProjection(Collider *collider, float projectedPosition, bool end)
    : collider(collider), projectedPosition(projectedPosition), isEnd(end)
{
}

BoundingRadiusProjection::~BoundingRadiusProjection()
{
}

Collider *BoundingRadiusProjection::getCollider()
{
    return collider;
}

float BoundingRadiusProjection::getProjectedPosition() const
{
    return projectedPosition;
}

bool BoundingRadiusProjection::getIsEnd() const
{
    return isEnd;
}

void BoundingRadiusProjection::setCollider(Collider *collider)
{
    this->collider = collider;
}

void BoundingRadiusProjection::setProjectedPosition(float projectedPosition)
{
    this->projectedPosition = projectedPosition;
}

void BoundingRadiusProjection::setIsEnd(bool end)
{
    this->isEnd = end;
}

bool BoundingRadiusProjection::operator==(const BoundingRadiusProjection &other) const
{
    return this->projectedPosition == other.projectedPosition && this->isEnd == other.isEnd;
}

bool BoundingRadiusProjection::operator!=(const BoundingRadiusProjection &other) const
{
    return !(*this == other);
}

bool BoundingRadiusProjection::operator<(const BoundingRadiusProjection &other) const
{
    return projectedPosition < other.projectedPosition || (projectedPosition == other.projectedPosition && !isEnd && other.isEnd);
}

bool BoundingRadiusProjection::operator>(const BoundingRadiusProjection &other) const
{
    return projectedPosition > other.projectedPosition || (projectedPosition == other.projectedPosition && isEnd && !other.isEnd);
}

bool BoundingRadiusProjection::operator<=(const BoundingRadiusProjection &other) const
{
    return *this < other || *this == other;
}

bool BoundingRadiusProjection::operator>=(const BoundingRadiusProjection &other) const
{
    return *this > other || *this == other;
}
