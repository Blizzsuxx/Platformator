#include "collider.h"

BoundingRadiusProjectionProxy::BoundingRadiusProjectionProxy(BoundingRadiusProjection *projection, LocalSortArray *chunk) : projection(projection), chunk(chunk)
{
}

BoundingRadiusProjectionProxy::~BoundingRadiusProjectionProxy()
{
}

BoundingRadiusProjection *BoundingRadiusProjectionProxy::getProjection() const
{
    return projection;
}

LocalSortArray *BoundingRadiusProjectionProxy::getChunk() const
{
    return chunk;
}

void BoundingRadiusProjectionProxy::setBoundingProjection(BoundingRadiusProjection *projection)
{
    this->projection = projection;
}

void BoundingRadiusProjectionProxy::setChunk(LocalSortArray *chunk)
{
    this->chunk = chunk;
}

bool BoundingRadiusProjectionProxy::operator==(const BoundingRadiusProjectionProxy &other) const
{
    return *this->projection == *other.projection;
}

bool BoundingRadiusProjectionProxy::operator!=(const BoundingRadiusProjectionProxy &other) const
{
    return !(*this == other);
}

bool BoundingRadiusProjectionProxy::operator<(const BoundingRadiusProjectionProxy &other) const
{
    return *this->projection < *other.projection;
}

bool BoundingRadiusProjectionProxy::operator>(const BoundingRadiusProjectionProxy &other) const
{
    return *this->projection > *other.projection;
}

bool BoundingRadiusProjectionProxy::operator<=(const BoundingRadiusProjectionProxy &other) const
{
    return *this < other || *this == other;
}

bool BoundingRadiusProjectionProxy::operator>=(const BoundingRadiusProjectionProxy &other) const
{
    return *this > other || *this == other;
}

Collider *BoundingRadiusProjectionProxy::getCollider()
{
    return projection->getCollider();
}

float BoundingRadiusProjectionProxy::getProjectedPosition() const
{
    return projection->getProjectedPosition();
}

bool BoundingRadiusProjectionProxy::getIsMaxima() const
{
    return projection->getIsMaxima();
}