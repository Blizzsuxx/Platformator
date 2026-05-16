#include "gridcell.h"

GridCell::GridCell(GridCellKey key, Grid *owner)
    : aabb(owner), key(key), queuedForEmpty(false), queuedForOperations(false), collidersToAdd(), bindingsToRemove(), bindingsToSync()
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

AABB *GridCell::getAABB()
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
        aabb.repair(binding);
    }
    bindingsToSync.clear();

    for (Collider *collider : collidersToAdd)
    {
        addCollider(collider);
    }
    collidersToAdd.clear();

    clearQueuedForOperations();
}