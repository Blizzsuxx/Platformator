#include "collider.h"

BoundingRadiusProjectionAxis::BoundingRadiusProjectionAxis(Collider *collider, float min, float max)
    : min(BoundingRadiusProjection(collider, min, false)),
      max(BoundingRadiusProjection(collider, max, true))
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