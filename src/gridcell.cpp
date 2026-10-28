#include "gridcell.h"
#include "grid.h"

GridCell::GridCell(GridCellKey key, Grid *owner)
    : preparedEndpoints(owner && owner->usesPreparedBounds()), aabb(owner), key(key), queuedForEmpty(false), queuedForOperations(false), collidersToAdd(), bindingsToRemove(), bindingsToSync()
{
}

GridCell::GridCell(GridCellKey key, AABB::PairPolicy pairPolicy)
    : aabb(pairPolicy), key(key), queuedForEmpty(false), queuedForOperations(false), collidersToAdd(),
      bindingsToRemove(), bindingsToSync()
{
}

GridCell::~GridCell()
{
}

void GridCell::addCollider(Collider *collider)
{
    aabb.add(collider);
}

void GridCell::removeCollider(Collider *collider)
{
    aabb.remove(collider);
}

void GridCell::addEndpoint(ProjectionOwner endpoint, BoundingRadiusProjectionAxis *xAxis,
                           BoundingRadiusProjectionAxis *yAxis)
{
    aabb.add(endpoint, xAxis, yAxis);
}

void GridCell::removeEndpoint(ProjectionOwner endpoint)
{
    aabb.remove(endpoint);
}

void GridCell::repairEndpoint(ProjectionOwner endpoint)
{
    aabb.repair(endpoint);
}

AABB *GridCell::getAABB()
{
    return &aabb;
}

const AABB *GridCell::getAABB() const
{
    return &aabb;
}

const GridCellKey &GridCell::getCellKey() const
{
    return key;
}

bool GridCell::markQueuedForEmpty()
{
    return !queuedForEmpty.exchange(true, std::memory_order_acq_rel);
}

void GridCell::clearQueuedForEmpty()
{
    queuedForEmpty.store(false, std::memory_order_release);
}

bool GridCell::markQueuedForOperations()
{
    return !queuedForOperations.exchange(true, std::memory_order_acq_rel);
}

void GridCell::clearQueuedForOperations()
{
    queuedForOperations.store(false, std::memory_order_release);
}

void GridCell::queueRemoveBinding(AABB::ColliderBinding *binding)
{
    if (binding != nullptr)
    {
        bindingsToRemove.push_back(binding);
    }
}

void GridCell::queueAddCollider(Collider *collider)
{
    collidersToAdd.push_back(collider);
}

void GridCell::queueSyncBinding(AABB::ColliderBinding *binding)
{
    if (binding != nullptr)
    {
        bindingsToSync.push_back(binding);
    }
}

void GridCell::flushPendingOperations()
{
    for (AABB::ColliderBinding *binding : bindingsToRemove)
    {
        aabb.remove(binding);
    }
    bindingsToRemove.clear();

    for (AABB::ColliderBinding *binding : bindingsToSync)
    {
        if (binding == nullptr || aabb.pairPolicy.endpointKind != ProjectionOwnerKind::Collider ||
            binding->endpoint == nullptr)
        {
            continue;
        }

        auto *collider = static_cast<Collider *>(binding->endpoint);
        if (collider->getIsQueuedForRemove() || (!preparedEndpoints && (collider->getGameObject()->getIsMarkedForDeletion() ||
            !collider->getGameObject()->getActive())))
            continue;
        aabb.repair(binding);
    }
    bindingsToSync.clear();

    for (Collider *collider : collidersToAdd)
    {
        if (collider == nullptr || collider->getIsQueuedForRemove() || (!preparedEndpoints && (collider->getGameObject()->getIsMarkedForDeletion() || !collider->getGameObject()->getActive())))
        {
            continue;
        }

        addCollider(collider);
    }
    collidersToAdd.clear();

    clearQueuedForOperations();
}

size_t GridCell::getOwnedCapacityBytes() const
{
    return aabb.getOwnedCapacityBytes() + collidersToAdd.capacity() * sizeof(Collider *) +
           (bindingsToRemove.capacity() + bindingsToSync.capacity()) * sizeof(AABB::ColliderBinding *);
}
