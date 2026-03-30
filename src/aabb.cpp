#include "aabb.h"
#include "grid.h"

AABB::AABB(Grid *owner)
    : intervalListX(this, Axis::X, true),
      intervalListY(this, Axis::Y, false),
      pairsWithAtLeastOneAxisOverlapping(),
      owner(owner)
{
    pairsWithAtLeastOneAxisOverlapping.reserve(8);
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

size_t AABB::findPairIndex(Collider *colliderA, Collider *colliderB) const
{
    for (size_t i = 0; i < pairsWithAtLeastOneAxisOverlapping.size(); ++i)
    {
        const AABBPair &pair = pairsWithAtLeastOneAxisOverlapping[i];
        if ((pair.a == colliderA && pair.b == colliderB) || (pair.a == colliderB && pair.b == colliderA))
        {
            return i;
        }
    }

    return SIZE_MAX;
}

void AABB::axisOverlapBegin(Collider *colliderA, Collider *colliderB, Axis axis)
{
    size_t pairIndex = findPairIndex(colliderA, colliderB);
    if (pairIndex == SIZE_MAX)
    {
        pairIndex = pairsWithAtLeastOneAxisOverlapping.size();
        pairsWithAtLeastOneAxisOverlapping.emplace_back(colliderA, colliderB);
    }

    AABBPair &pair = pairsWithAtLeastOneAxisOverlapping[pairIndex];
    uint8_t previousAxisOverlap = pair.axisOverlap;
    pair.axisOverlap |= axis;

    if (pair.axisOverlap == Axis::ALL_AXES && previousAxisOverlap != Axis::ALL_AXES)
    {
        if (!colliderA->getGameObject()->getActive() || !colliderB->getGameObject()->getActive())
        {
            return;
        }
        if ((colliderA->getCollisionGroup() & colliderB->getCollisionMask()) == 0 || (colliderB->getCollisionGroup() & colliderA->getCollisionMask()) == 0)
        {
            return;
        }

        // Both axes are now overlapping in this cell, so this cell contributes one witness.
        owner->createCollisionPair(colliderA, colliderB);
    }
}

void AABB::axisOverlapEnd(Collider *colliderA, Collider *colliderB, Axis axis)
{
    size_t pairIndex = findPairIndex(colliderA, colliderB);
    if (pairIndex == SIZE_MAX)
    {
        return;
    }

    AABBPair &pair = pairsWithAtLeastOneAxisOverlapping[pairIndex];
    uint8_t previousAxisOverlap = pair.axisOverlap;
    pair.axisOverlap &= ~axis;

    if (pair.axisOverlap == 0)
    {
        size_t lastIndex = pairsWithAtLeastOneAxisOverlapping.size() - 1;
        if (pairIndex != lastIndex)
        {
            pairsWithAtLeastOneAxisOverlapping[pairIndex] = pairsWithAtLeastOneAxisOverlapping[lastIndex];
        }
        pairsWithAtLeastOneAxisOverlapping.pop_back();
    }

    if (previousAxisOverlap == Axis::ALL_AXES && pair.axisOverlap != Axis::ALL_AXES)
    {
        owner->removeCollisionPair(colliderA, colliderB);
    }
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