#include "boundingradiousprojection.h"

BoundingRadiusProjection::BoundingRadiusProjection()
{
}

BoundingRadiusProjection::BoundingRadiusProjection(GameObject* gameObject, float min, float max)
    : gameObject(gameObject), min(min), max(max)
{
}

BoundingRadiusProjection::~BoundingRadiusProjection()
{
}

GameObject* BoundingRadiusProjection::getGameObject()
{
    return gameObject;
}

float BoundingRadiusProjection::getMin() const
{
    return min;
}

float BoundingRadiusProjection::getMax() const
{
    return max;
}

void BoundingRadiusProjection::setGameObject(GameObject* gameObject)
{
    this->gameObject = gameObject;
}

void BoundingRadiusProjection::setMin(float min)
{
    this->min = min;
}

void BoundingRadiusProjection::setMax(float max)
{
    this->max = max;
}

bool BoundingRadiusProjection::isOverlapping(BoundingRadiusProjection* other)
{
    return (min <= other->getMax() && max >= other->getMin()) || (other->getMin() <= max && other->getMax() >= min);
}

bool BoundingRadiusProjection::operator==(const BoundingRadiusProjection& other) const
{
    return min == other.min && max == other.max;
}

bool BoundingRadiusProjection::operator!=(const BoundingRadiusProjection& other) const
{
    return !(*this == other);
}

bool BoundingRadiusProjection::operator<(const BoundingRadiusProjection& other) const
{
    if (min == other.min)
    {
        return max < other.max;
    }

    return min < other.min;
}

bool BoundingRadiusProjection::operator>(const BoundingRadiusProjection& other) const
{
    if (min == other.min)
    {
        return max > other.max;
    }

    return min > other.min;
}

bool BoundingRadiusProjection::operator<=(const BoundingRadiusProjection& other) const
{
    return *this < other || *this == other;
}

bool BoundingRadiusProjection::operator>=(const BoundingRadiusProjection& other) const
{
    return *this > other || *this == other;
}
