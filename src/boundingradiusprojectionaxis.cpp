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

bool BoundingRadiusProjectionAxis::operator==(const BoundingRadiusProjectionAxis &other) const
{
    return min == other.min && max == other.max;
}

bool BoundingRadiusProjectionAxis::operator!=(const BoundingRadiusProjectionAxis &other) const
{
    return !(*this == other);
}

BoundingRadiusProjectionAxisBinding::BoundingRadiusProjectionAxisBinding(BoundingRadiusProjectionAxis *axis)
    : minProxy(axis->getMin(), nullptr),
      maxProxy(axis->getMax(), nullptr)
{
}

void BoundingRadiusProjectionAxisBinding::bind(BoundingRadiusProjectionAxis *axis)
{
    minProxy.setBoundingProjection(axis->getMin());
    minProxy.setChunk(nullptr);
    minProxy.setChunkIndex(SIZE_MAX);
    minProxy.updateCachedProjectedPosition();

    maxProxy.setBoundingProjection(axis->getMax());
    maxProxy.setChunk(nullptr);
    maxProxy.setChunkIndex(SIZE_MAX);
    maxProxy.updateCachedProjectedPosition();
}