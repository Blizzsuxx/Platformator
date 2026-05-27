#include "aabb.h"
#include "grid.h"

AABB::AABB(Grid *owner)
    : intervalListX(this, Axis::X, true),
      intervalListY(this, Axis::Y, false),
      pairsWithBothAxisOverlapping(),
      colliderBindings(),
      owner(owner)
{
    pairsWithBothAxisOverlapping.reserve(8);
}

AABB::~AABB()
{
}

void AABB::add(Collider *element)
{
    // auto iterator = colliderBindings.find(element);
    // if (iterator != colliderBindings.end())
    // {
    //     PLATFORMATOR_LOG("Warning: trying to add a collider to AABB that is already in it, collider: %s\n", element->getGameObject()->getName().c_str());
    //     return;
    // }
    auto result = colliderBindings.try_emplace(element, element);
    ColliderBinding *binding = &result.first->second;
    intervalListX.add(&binding->xBinding);
    intervalListY.add(&binding->yBinding);
}

void AABB::remove(Collider *element)
{
    ColliderBinding *binding = getBinding(element);
    if (binding == nullptr)
    {
        return;
    }

    remove(binding);
}

void AABB::remove(ColliderBinding *binding)
{
    if (binding == nullptr)
    {
        return;
    }

    intervalListX.remove(&binding->xBinding);
    intervalListY.remove(&binding->yBinding);
    colliderBindings.erase(binding->collider);
}

void AABB::repair(Collider *element)
{
    ColliderBinding *binding = getBinding(element);
    if (binding == nullptr)
    {
        return;
    }

    repair(binding);
}

void AABB::repair(ColliderBinding *binding)
{
    binding->xBinding.minProxy.updateCachedProjectedPosition();
    intervalListX.repairProjection(&binding->xBinding.minProxy);

    binding->xBinding.maxProxy.updateCachedProjectedPosition();
    intervalListX.repairProjection(&binding->xBinding.maxProxy);

    binding->yBinding.minProxy.updateCachedProjectedPosition();
    intervalListY.repairProjection(&binding->yBinding.minProxy);

    binding->yBinding.maxProxy.updateCachedProjectedPosition();
    intervalListY.repairProjection(&binding->yBinding.maxProxy);
}

AABB::ColliderBinding *AABB::getBinding(Collider *element)
{
    auto iterator = colliderBindings.find(element);
    if (iterator == colliderBindings.end())
    {
        return nullptr;
    }

    return &iterator->second;
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
    bool isOverlappingOnBothAxes = checkOverlapOnAxis(colliderA, colliderB);

    if (!isOverlappingOnBothAxes)
    {
        return;
    }

    AABBPair pair(colliderA, colliderB);
    auto iterator = pairsWithBothAxisOverlapping.find(pair);
    if (iterator != pairsWithBothAxisOverlapping.end())
    {
        return;
    }

    if (!colliderA->getGameObject()->getActive() || !colliderB->getGameObject()->getActive())
    {
        return;
    }
    if ((colliderA->getCollisionGroup() & colliderB->getCollisionMask()) == 0 || (colliderB->getCollisionGroup() & colliderA->getCollisionMask()) == 0)
    {
        return;
    }

    pairsWithBothAxisOverlapping.insert(pair);
    owner->recordPairDelta(colliderA, colliderB, 1);
}

void AABB::axisOverlapEnd(Collider *colliderA, Collider *colliderB, Axis axis)
{
    AABBPair pair(colliderA, colliderB);
    auto iterator = pairsWithBothAxisOverlapping.find(pair);
    if (iterator == pairsWithBothAxisOverlapping.end())
    {
        return;
    }
    pairsWithBothAxisOverlapping.erase(iterator);

    owner->recordPairDelta(colliderA, colliderB, -1);
}

void AABB::overlapBeginCheckpoint(Collider *colliderA, Collider *colliderB)
{
    axisOverlapBegin(colliderA, colliderB, Axis::ALL_AXES);
}

void AABB::overlapEndCheckpoint(Collider *colliderA, Collider *colliderB)
{
    axisOverlapEnd(colliderA, colliderB, Axis::ALL_AXES);
}

bool AABB::checkOverlapOnAxis(Collider *colliderA, Collider *colliderB) const
{
    bool xOverlap = (*colliderA->getXProjections()->getMax() >= *colliderB->getXProjections()->getMin() && *colliderA->getXProjections()->getMin() <= *colliderB->getXProjections()->getMax());
    bool yOverlap = (*colliderA->getYProjections()->getMax() >= *colliderB->getYProjections()->getMin() && *colliderA->getYProjections()->getMin() <= *colliderB->getYProjections()->getMax());

    return xOverlap && yOverlap;
}

bool AABB::getIsEmpty() const
{
    // checking just one is enough because if one is empty, the other one has to be empty as well
    return intervalListX.getIsEmpty();
}