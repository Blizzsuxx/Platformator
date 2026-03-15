#include "collider.h"

BoundingRadiusProjectionAxis::BoundingRadiusProjectionAxis(Collider *collider, float min, float max)
    : min(BoundingRadiusProjection(collider, min, false, nullptr)),
      max(BoundingRadiusProjection(collider, max, true, nullptr))
{
}

BoundingRadiusProjectionAxis::~BoundingRadiusProjectionAxis()
{
}

BoundingRadiusProjection *BoundingRadiusProjectionAxis::getMin()
{
    return &min;
}

BoundingRadiusProjection *BoundingRadiusProjectionAxis::getMax()
{
    return &max;
}

bool BoundingRadiusProjectionAxis::operator==(const BoundingRadiusProjectionAxis &other) const
{
    return min == other.min && max == other.max;
}

bool BoundingRadiusProjectionAxis::operator!=(const BoundingRadiusProjectionAxis &other) const
{
    return !(*this == other);
}