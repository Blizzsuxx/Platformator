#include "aabb.h"
#include <iostream>

AABB::AABB()
    : intervalListX(this, Axis::X),
      intervalListY(this, Axis::Y),
      candidateCollisions(),
      pairAdjacency()
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
    removePairsForCollider(element);
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

    auto [iterator, inserted] = candidateCollisions.emplace(colliderA, colliderB);
    if (!inserted)
    {
        return;
    }

    Collider *objectA = iterator->getObjectA();
    Collider *objectB = iterator->getObjectB();

    pairAdjacency[objectA].insert(objectB);
    pairAdjacency[objectB].insert(objectA);
}

void AABB::axisOverlapEnd(Collider *colliderA, Collider *colliderB, Axis axis)
{
    removePair(ColliderPair(colliderA, colliderB));
}

void AABB::sort()
{
    // TODO: parallelize this
    intervalListX.sort();
    intervalListY.sort();
}

void AABB::removePair(const ColliderPair &pair)
{
    auto iterator = candidateCollisions.find(pair);
    if (iterator == candidateCollisions.end())
    {
        return;
    }

    Collider *objectA = iterator->getObjectA();
    Collider *objectB = iterator->getObjectB();

    candidateCollisions.erase(iterator);

    auto adjacencyA = pairAdjacency.find(objectA);
    if (adjacencyA != pairAdjacency.end())
    {
        adjacencyA->second.erase(objectB);
        if (adjacencyA->second.empty())
        {
            pairAdjacency.erase(adjacencyA);
        }
    }

    auto adjacencyB = pairAdjacency.find(objectB);
    if (adjacencyB != pairAdjacency.end())
    {
        adjacencyB->second.erase(objectA);
        if (adjacencyB->second.empty())
        {
            pairAdjacency.erase(adjacencyB);
        }
    }
}

void AABB::removePairsForCollider(Collider *collider)
{
    auto adjacencyIterator = pairAdjacency.find(collider);
    if (adjacencyIterator == pairAdjacency.end())
    {
        return;
    }

    std::vector<Collider *> touchingColliders(adjacencyIterator->second.begin(), adjacencyIterator->second.end());
    for (Collider *otherCollider : touchingColliders)
    {
        removePair(ColliderPair(collider, otherCollider));
    }
}