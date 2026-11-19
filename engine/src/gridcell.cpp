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
    if (queuedForEmpty)
    {
        return false;
    }
    queuedForEmpty = true;
    return true;
}

void GridCell::clearQueuedForEmpty()
{
    queuedForEmpty = false;
}

bool GridCell::markQueuedForOperations()
{
    if (queuedForOperations)
    {
        return false;
    }
    queuedForOperations = true;
    return true;
}

void GridCell::clearQueuedForOperations()
{
    queuedForOperations = false;
}

void GridCell::queueRemoveCollider(Collider *collider)
{
    if (collider != nullptr)
    {
        collidersToRemove.push_back(collider);
    }
}

void GridCell::queueAddCollider(Collider *collider)
{
    collidersToAdd.push_back(collider);
}

void GridCell::queueSyncCollider(Collider *collider)
{
    if (collider != nullptr)
    {
        collidersToSync.push_back(collider);
    }
}

void GridCell::flushPendingOperations()
{
    for (Collider *collider : collidersToRemove)
    {
        aabb.remove(collider);
    }
    collidersToRemove.clear();

    for (Collider *collider : collidersToSync)
    {
        if (collider == nullptr || collider->getIsQueuedForRemove() || collider->getGameObject()->getIsMarkedForDeletion() || !collider->getGameObject()->getActive())
        {
            continue;
        }

        aabb.repair(collider);
    }
    collidersToSync.clear();

    for (Collider *collider : collidersToAdd)
    {
        if (collider == nullptr || collider->getIsQueuedForRemove() || collider->getGameObject()->getIsMarkedForDeletion() || !collider->getGameObject()->getActive())
        {
            continue;
        }

        addCollider(collider);
    }
    collidersToAdd.clear();

    clearQueuedForOperations();
}