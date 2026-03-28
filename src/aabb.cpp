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

    BoundingRadiusProjection *minA;
    BoundingRadiusProjection *maxA;
    BoundingRadiusProjection *minB;
    BoundingRadiusProjection *maxB;

    if (axis == Axis::X)
    {
        minA = colliderA->getYProjections()->getMin();
        maxA = colliderA->getYProjections()->getMax();
        minB = colliderB->getYProjections()->getMin();
        maxB = colliderB->getYProjections()->getMax();
    }
    else
    {
        minA = colliderA->getXProjections()->getMin();
        maxA = colliderA->getXProjections()->getMax();
        minB = colliderB->getXProjections()->getMin();
        maxB = colliderB->getXProjections()->getMax();
    }

    if (*minA > *maxB || *minB > *maxA)
    {
        return;
    }

    owner->createCollisionPair(colliderA, colliderB);
}

void AABB::axisOverlapEnd(Collider *colliderA, Collider *colliderB, Axis axis)
{
    owner->removeCollisionPair(colliderA, colliderB);
}

void AABB::sort()
{
    // TODO: parallelize this
    intervalListX.sort();
    intervalListY.sort();
}