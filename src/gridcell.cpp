#include "gridcell.h"

GridCell::GridCell(GridCellKey key, Grid *owner)
    : aabb(owner), key(key), flags(0), collidersToAdd(), collidersToRemove(), collidersToSync()
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

bool GridCell::getIsQueuedForEmpty() const
{
    return (flags & GridCellFlags::QueuedForEmpty) != 0;
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

void GridCell::setIsQueuedForEmpty(bool isQueuedForEmpty)
{
    if (isQueuedForEmpty)
    {
        flags |= GridCellFlags::QueuedForEmpty;
    }
    else
    {
        flags &= ~GridCellFlags::QueuedForEmpty;
    }
}

bool GridCell::getIsQueuedForOperations() const
{
    return (flags & GridCellFlags::QueuedForOperations) != 0;
}

void GridCell::setIsQueuedForOperations(bool isQueuedForOperations)
{
    if (isQueuedForOperations)
    {
        flags |= GridCellFlags::QueuedForOperations;
    }
    else
    {
        flags &= ~GridCellFlags::QueuedForOperations;
    }
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
}