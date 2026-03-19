#include "collider.h"
#include "localsortarray.h"

BoundingRadiusProjection::BoundingRadiusProjection() : collider(nullptr), projectedPosition(0.0f), isMaxima(false), chunk(nullptr)
{
}

BoundingRadiusProjection::BoundingRadiusProjection(Collider *collider, float projectedPosition, bool end, LocalSortArray *chunk)
    : collider(collider), projectedPosition(projectedPosition), isMaxima(end), chunk(chunk)
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

bool BoundingRadiusProjection::getIsMaxima() const
{
    return isMaxima;
}

LocalSortArray *BoundingRadiusProjection::getChunk() const
{
    return chunk;
}

void BoundingRadiusProjection::setCollider(Collider *collider)
{
    this->collider = collider;
}

void BoundingRadiusProjection::setProjectedPosition(float projectedPosition)
{
    this->projectedPosition = projectedPosition;
}

void BoundingRadiusProjection::setIsMaxima(bool end)
{
    this->isMaxima = end;
}

void BoundingRadiusProjection::setChunk(LocalSortArray *chunk)
{
    this->chunk = chunk;
}

bool BoundingRadiusProjection::operator==(const BoundingRadiusProjection &other) const
{
    return this->projectedPosition == other.projectedPosition && this->isMaxima == other.isMaxima;
}

bool BoundingRadiusProjection::operator!=(const BoundingRadiusProjection &other) const
{
    return !(*this == other);
}

bool BoundingRadiusProjection::operator<(const BoundingRadiusProjection &other) const
{
    return projectedPosition < other.projectedPosition || (projectedPosition == other.projectedPosition && !isMaxima && other.isMaxima);
}

bool BoundingRadiusProjection::operator>(const BoundingRadiusProjection &other) const
{
    return projectedPosition > other.projectedPosition || (projectedPosition == other.projectedPosition && isMaxima && !other.isMaxima);
}

bool BoundingRadiusProjection::operator<=(const BoundingRadiusProjection &other) const
{
    return *this < other || *this == other;
}

bool BoundingRadiusProjection::operator>=(const BoundingRadiusProjection &other) const
{
    return *this > other || *this == other;
}
