#include "aabb.h"
#include "grid.h"

AABB::AABB(Grid *owner)
    : intervalListX(this, Axis::X, true),
      intervalListY(this, Axis::Y, false),
      pairsWithAtLeastOneAxisOverlapping(),
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
    auto iterator = pairsWithAtLeastOneAxisOverlapping.find(AABBPair(colliderA, colliderB));
    if (iterator == pairsWithAtLeastOneAxisOverlapping.end())
    {
        auto result = pairsWithAtLeastOneAxisOverlapping.insert(AABBPair(colliderA, colliderB));
        iterator = result.first;
    }

    uint8_t previousAxisOverlap = iterator->axisOverlap;
    AABBPair &pair = const_cast<AABBPair &>(*iterator);
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
    auto iterator = pairsWithAtLeastOneAxisOverlapping.find(AABBPair(colliderA, colliderB));
    if (iterator == pairsWithAtLeastOneAxisOverlapping.end())
    {
        return;
    }

    uint8_t previousAxisOverlap = iterator->axisOverlap;
    AABBPair &pair = const_cast<AABBPair &>(*iterator);
    pair.axisOverlap &= ~axis;

    if (pair.axisOverlap == 0)
    {
        // no axes are overlapping anymore, we can remove the collision
        pairsWithAtLeastOneAxisOverlapping.erase(iterator);
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