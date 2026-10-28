#include "platformator/collider.h"

BoundingRadiusProjectionAxis::BoundingRadiusProjectionAxis(Collider *collider, float min, float max)
    : min(BoundingRadiusProjection(collider, min, false)), max(BoundingRadiusProjection(collider, max, true))
{
}

BoundingRadiusProjectionAxis::BoundingRadiusProjectionAxis(void *owner, ProjectionOwnerKind ownerKind, float min, float max)
    : min(BoundingRadiusProjection(owner, ownerKind, min, false)),
      max(BoundingRadiusProjection(owner, ownerKind, max, true))
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

BoundingRadiusProjectionAxisBinding::BoundingRadiusProjectionAxisBinding(BoundingRadiusProjectionAxis *axis)
    : minProxy(axis->getMin(), nullptr), maxProxy(axis->getMax(), nullptr)
{
}
