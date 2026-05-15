#include "gridcell.h"

GridCell::GridCell(GridCellKey key, Grid *owner)
    : aabb(owner), key(key), queuedForEmpty(false), queuedForOperations(false), collidersToAdd(), collidersToRemove(), collidersToSync()
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

void GridCell::queueRemoveCollider(Collider *collider)
{
    collidersToRemove.push_back(collider);
}

void GridCell::queueAddCollider(Collider *collider)
{
    collidersToAdd.push_back(collider);
}

void GridCell::queueSyncCollider(Collider *collider)
{
    collidersToSync.push_back(collider);
}

void GridCell::flushPendingOperations()
{
    for (Collider *collider : collidersToRemove)
    {
        removeCollider(collider);
    }
    collidersToRemove.clear();

    for (Collider *collider : collidersToSync)
    {
        collider->repairProjectionsForCell(this);
    }
    collidersToSync.clear();

    for (Collider *collider : collidersToAdd)
    {
        addCollider(collider);
    }
    collidersToAdd.clear();

    clearQueuedForOperations();
}