#include "collider.h"
#include <cstddef>

BoundingRadiusProjectionProxy::BoundingRadiusProjectionProxy(BoundingRadiusProjection *projection, LocalSortArray *chunk) : projection(projection), chunk(chunk), chunkIndex(SIZE_MAX), isDirty(false)
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

size_t BoundingRadiusProjectionProxy::getChunkIndex() const
{
    return chunkIndex;
}

bool BoundingRadiusProjectionProxy::getIsDirty() const
{
    return isDirty;
}

void BoundingRadiusProjectionProxy::setBoundingProjection(BoundingRadiusProjection *projection)
{
    this->projection = projection;
}

void BoundingRadiusProjectionProxy::setChunk(LocalSortArray *chunk)
{
    this->chunk = chunk;
    if (chunk == nullptr)
    {
        chunkIndex = SIZE_MAX;
    }
}

void BoundingRadiusProjectionProxy::setChunkIndex(size_t chunkIndex)
{
    this->chunkIndex = chunkIndex;
}

void BoundingRadiusProjectionProxy::setIsDirty(bool dirty)
{
    isDirty = dirty;
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