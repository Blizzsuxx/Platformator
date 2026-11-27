#include "physics/broadphase/gridcell.h"

GridCell::GridCell(GridCellKey key, Grid *owner)
    : aabb(owner), key(key), queuedForEmpty(false), queuedForOperations(false), collidersToAdd(), collidersToRemove(), collidersToSync()
{
}

GridCell::~GridCell() = default;

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
    if (collider != nullptr)
    {
        collidersToAdd.push_back(collider);
    }
}

void GridCell::queueSyncCollider(Collider *collider)
{
    if (collider != nullptr)
    {
        collidersToSync.push_back(collider);
    }
}

namespace
{
inline bool isColliderInactiveForCell(const Collider *collider) noexcept
{
    return collider == nullptr || collider->getIsQueuedForRemove() || collider->isRetired() ||
           !collider->getIsOwnerActive();
}
} // namespace

void GridCell::flushPendingOperations()
{
    aabb.beginDeltaBatch();
    if (!collidersToRemove.empty())
    {
        aabb.removeBatch(collidersToRemove);
        collidersToRemove.clear();
    }

    for (Collider *collider : collidersToSync)
    {
        if (isColliderInactiveForCell(collider))
        {
            continue;
        }

        aabb.repair(collider);
    }
    collidersToSync.clear();

    if (!collidersToAdd.empty())
    {
        std::erase_if(collidersToAdd, [](const Collider *collider) { return isColliderInactiveForCell(collider); });
        aabb.addBatch(collidersToAdd);
        collidersToAdd.clear();
    }

    aabb.endDeltaBatch();
    clearQueuedForOperations();
}