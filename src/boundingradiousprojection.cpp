#include "platformator/collider.h"
#include "localsortarray.h"

BoundingRadiusProjection::BoundingRadiusProjection()
    : owner(nullptr), projectedPosition(0.0f), ownerKind(ProjectionOwnerKind::Collider), isMaxima(false)
{
}

BoundingRadiusProjection::BoundingRadiusProjection(Collider *collider, float projectedPosition, bool end)
    : BoundingRadiusProjection(static_cast<void *>(collider), ProjectionOwnerKind::Collider, projectedPosition, end)
{
}

BoundingRadiusProjection::BoundingRadiusProjection(void *owner, ProjectionOwnerKind ownerKind, float projectedPosition,
                                                   bool end)
    : owner(owner), projectedPosition(projectedPosition), ownerKind(ownerKind), isMaxima(end)
{
}

BoundingRadiusProjection::~BoundingRadiusProjection()
{
}

Collider *BoundingRadiusProjection::getCollider()
{
    return ownerKind == ProjectionOwnerKind::Collider ? static_cast<Collider *>(owner) : nullptr;
}

void *BoundingRadiusProjection::getOwner() const
{
    return owner;
}

ProjectionOwnerKind BoundingRadiusProjection::getOwnerKind() const
{
    return ownerKind;
}

ProjectionOwner BoundingRadiusProjection::getEndpoint() const
{
    return {owner, ownerKind};
}

float BoundingRadiusProjection::getProjectedPosition() const
{
    return projectedPosition;
}

bool BoundingRadiusProjection::getIsMaxima() const
{
    return isMaxima;
}

void BoundingRadiusProjection::setCollider(Collider *collider)
{
    setOwner(collider, ProjectionOwnerKind::Collider);
}

void BoundingRadiusProjection::setOwner(void *owner, ProjectionOwnerKind ownerKind)
{
    this->owner = owner;
    this->ownerKind = ownerKind;
}

void BoundingRadiusProjection::setProjectedPosition(float projectedPosition)
{
    this->projectedPosition = projectedPosition;
}

void BoundingRadiusProjection::setIsMaxima(bool end)
{
    this->isMaxima = end;
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
    return projectedPosition < other.projectedPosition ||
           (projectedPosition == other.projectedPosition && !isMaxima && other.isMaxima);
}

bool BoundingRadiusProjection::operator>(const BoundingRadiusProjection &other) const
{
    return projectedPosition > other.projectedPosition ||
           (projectedPosition == other.projectedPosition && isMaxima && !other.isMaxima);
}

bool BoundingRadiusProjection::operator<=(const BoundingRadiusProjection &other) const
{
    return *this < other || *this == other;
}

bool BoundingRadiusProjection::operator>=(const BoundingRadiusProjection &other) const
{
    return *this > other || *this == other;
}
