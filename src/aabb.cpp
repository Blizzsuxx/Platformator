#include "aabb.h"
#include <iostream>

AABB::AABB()
    : intervalListX(this, Axis::X),
      intervalListY(this, Axis::Y),
      candidateCollisions(),
      pairsToRemove()
{
}

AABB::~AABB()
{
    removeMarkedPairs();
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

const std::unordered_set<ColliderPair, ColliderPair::HashFunction> *AABB::getCandidatePairSet() const
{
    return &candidateCollisions;
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

    candidateCollisions.emplace(colliderA, colliderB);
}

void AABB::axisOverlapEnd(Collider *colliderA, Collider *colliderB, Axis axis)
{
    candidateCollisions.erase(ColliderPair(colliderA, colliderB));
}

void AABB::sort()
{
    // TODO: parallelize this
    intervalListX.sort();
    intervalListY.sort();
}

void AABB::markPairForRemoval(const ColliderPair &pair)
{
    pairsToRemove.push_back(pair);
}

void AABB::removeMarkedPairs()
{
    for (const ColliderPair &pair : pairsToRemove)
    {
        candidateCollisions.erase(pair);
    }
    pairsToRemove.clear();
}