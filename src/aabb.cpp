#include "aabb.h"
#include <iostream>
#include "grid.h"

AABB::AABB(Grid *owner)
    : intervalListX(this, Axis::X, true),
      intervalListY(this, Axis::Y, false),
      owner(owner)
{
}

AABB::~AABB()
{
}

void AABB::add(Collider *element)
{
    intervalListX.add(element->getXProjections());
    intervalListY.add(element->getYProjections());
}

void AABB::remove(Collider *element)
{
    intervalListX.remove(element->getXProjections());
    intervalListY.remove(element->getYProjections());
}

SegmentedIntervalList *AABB::getIntervalListX()
{
    return &intervalListX;
}

SegmentedIntervalList *AABB::getIntervalListY()
{
    return &intervalListY;
}

void AABB::axisOverlapBegin(Collider *colliderA, Collider *colliderB, Axis axis)
{
    if (colliderA->getGameObject()->getActive() == false || colliderB->getGameObject()->getActive() == false)
    {
        return;
    }
    if ((colliderA->getCollisionGroup() & colliderB->getCollisionMask()) == 0 || (colliderB->getCollisionGroup() & colliderA->getCollisionMask()) == 0)
    {
        return;
    }

    BoundingRadiusProjection *minXA = colliderA->getXProjections()->getMin();
    BoundingRadiusProjection *maxXA = colliderA->getXProjections()->getMax();
    BoundingRadiusProjection *minXB = colliderB->getXProjections()->getMin();
    BoundingRadiusProjection *maxXB = colliderB->getXProjections()->getMax();
    BoundingRadiusProjection *minYA = colliderA->getYProjections()->getMin();
    BoundingRadiusProjection *maxYA = colliderA->getYProjections()->getMax();
    BoundingRadiusProjection *minYB = colliderB->getYProjections()->getMin();
    BoundingRadiusProjection *maxYB = colliderB->getYProjections()->getMax();

    if (*minXA > *maxXB || *minXB > *maxXA || *minYA > *maxYB || *minYB > *maxYA)
    {
        return;
    }

    owner->createCollisionPair(colliderA, colliderB, axis);
}

void AABB::axisOverlapEnd(Collider *colliderA, Collider *colliderB, Axis axis)
{
    owner->removeCollisionPair(colliderA, colliderB, axis);
}

void AABB::sort()
{
    // TODO: parallelize this
    intervalListX.sort();
    intervalListY.sort();
}

bool AABB::getIsEmpty() const
{
    // checking just one is enough because if one is empty, the other one has to be empty as well
    return intervalListX.getIsEmpty();
}