#include "aabb.h"
#include <iostream>

AABB::AABB()
    : intervalListX(this, Axis::X, true),
      intervalListY(this, Axis::Y, false),
      candidateCollisions(),
      pendingNarrowPhasePairs(),
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

void AABB::removePairsForCollider(Collider *collider)
{
    const std::vector<const ColliderPair *> *touchingPairs = getTouchingPairs(collider);
    if (touchingPairs == nullptr)
    {
        return;
    }

    const std::vector<const ColliderPair *> pairsToRemove = *touchingPairs;
    for (const ColliderPair *pair : pairsToRemove)
    {
        removePair(*pair);
    }
}

void AABB::removePair(const ColliderPair &pair)
{
    auto iterator = candidateCollisions.find(pair);
    if (iterator == candidateCollisions.end())
    {
        return;
    }

    const ColliderPair *storedPair = &(*iterator);
    Collider *objectA = storedPair->getObjectA();
    Collider *objectB = storedPair->getObjectB();

    dequeuePairFromNarrowPhase(storedPair);
    storedPair->clearCollision();

    auto adjacencyA = pairAdjacency.find(objectA);
    if (adjacencyA != pairAdjacency.end())
    {
        std::vector<const ColliderPair *> &pairsA = adjacencyA->second;
        size_t removeIndexA = storedPair->getAdjacencyIndexA();
        size_t lastIndexA = pairsA.size() - 1;
        if (removeIndexA != lastIndexA)
        {
            const ColliderPair *movedPair = pairsA[lastIndexA];
            pairsA[removeIndexA] = movedPair;
            if (movedPair->getObjectA() == objectA)
            {
                movedPair->setAdjacencyIndexA(removeIndexA);
            }
            else
            {
                movedPair->setAdjacencyIndexB(removeIndexA);
            }
        }
        pairsA.pop_back();
        if (pairsA.empty())
        {
            pairAdjacency.erase(adjacencyA);
        }
    }

    auto adjacencyB = pairAdjacency.find(objectB);
    if (adjacencyB != pairAdjacency.end())
    {
        std::vector<const ColliderPair *> &pairsB = adjacencyB->second;
        size_t removeIndexB = storedPair->getAdjacencyIndexB();
        size_t lastIndexB = pairsB.size() - 1;
        if (removeIndexB != lastIndexB)
        {
            const ColliderPair *movedPair = pairsB[lastIndexB];
            pairsB[removeIndexB] = movedPair;
            if (movedPair->getObjectA() == objectB)
            {
                movedPair->setAdjacencyIndexA(removeIndexB);
            }
            else
            {
                movedPair->setAdjacencyIndexB(removeIndexB);
            }
        }
        pairsB.pop_back();
        if (pairsB.empty())
        {
            pairAdjacency.erase(adjacencyB);
        }
    }

    candidateCollisions.erase(iterator);
}

void AABB::dequeuePairFromNarrowPhase(const ColliderPair *pair)
{
    if (pair == nullptr || !pair->getIsQueuedForNarrowPhase())
    {
        return;
    }

    size_t removeIndex = pair->getNarrowPhaseQueueIndex();
    size_t lastIndex = pendingNarrowPhasePairs.size() - 1;

    if (removeIndex != lastIndex)
    {
        const ColliderPair *movedPair = pendingNarrowPhasePairs[lastIndex];
        pendingNarrowPhasePairs[removeIndex] = movedPair;
        movedPair->setNarrowPhaseQueueIndex(removeIndex);
    }

    pendingNarrowPhasePairs.pop_back();
    pair->setIsQueuedForNarrowPhase(false);
    pair->setNarrowPhaseQueueIndex(SIZE_MAX);
}

void AABB::queuePairsForCollider(Collider *collider)
{
    const std::vector<const ColliderPair *> *touchingPairs = getTouchingPairs(collider);
    if (touchingPairs == nullptr)
    {
        return;
    }

    for (const ColliderPair *pair : *touchingPairs)
    {
        queuePairForNarrowPhase(pair);
    }
}

void AABB::queuePairForNarrowPhase(const ColliderPair *pair)
{
    if (pair == nullptr || pair->getIsQueuedForNarrowPhase())
    {
        return;
    }

    pair->setIsQueuedForNarrowPhase(true);
    pair->setNarrowPhaseQueueIndex(pendingNarrowPhasePairs.size());
    pendingNarrowPhasePairs.push_back(pair);
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

const std::vector<const ColliderPair *> *AABB::getPendingNarrowPhasePairs() const
{
    return &pendingNarrowPhasePairs;
}

const std::vector<const ColliderPair *> *AABB::getTouchingPairs(Collider *collider) const
{
    auto iterator = pairAdjacency.find(collider);
    if (iterator == pairAdjacency.end())
    {
        return nullptr;
    }

    return &iterator->second;
}

void AABB::clearPendingNarrowPhasePairs()
{
    pendingNarrowPhasePairs.clear();
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

    const ColliderPair *pair = &(*iterator);
    Collider *objectA = pair->getObjectA();
    Collider *objectB = pair->getObjectB();

    std::vector<const ColliderPair *> &adjacencyA = pairAdjacency[objectA];
    pair->setAdjacencyIndexA(adjacencyA.size());
    adjacencyA.push_back(pair);

    std::vector<const ColliderPair *> &adjacencyB = pairAdjacency[objectB];
    pair->setAdjacencyIndexB(adjacencyB.size());
    adjacencyB.push_back(pair);

    queuePairForNarrowPhase(pair);
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