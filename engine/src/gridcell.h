#pragma once

#include "aabb.h"
#include "platformator/gridtypes.h"
#include <vector>

class Grid;

class GridCell
{
  public:
    GridCell(GridCellKey key, Grid *owner);
    ~GridCell();
    GridCell(const GridCell &) = delete;
    GridCell &operator=(const GridCell &) = delete;
    GridCell(GridCell &&) = delete;
    GridCell &operator=(GridCell &&) = delete;

    void addCollider(Collider *collider);
    void removeCollider(Collider *collider);
    AABB *getAABB();
    const AABB *getAABB() const;
    const GridCellKey &getCellKey() const;
    void resetKey(GridCellKey newKey) noexcept
    {
        key = newKey;
    }
    bool markQueuedForEmpty();
    void clearQueuedForEmpty();
    bool markQueuedForOperations();
    void clearQueuedForOperations();
    void queueRemoveCollider(Collider *collider);
    void queueAddCollider(Collider *collider);
    void queueSyncCollider(Collider *collider);
    void flushPendingOperations();

  private:
    AABB aabb;
    GridCellKey key;
    bool queuedForEmpty;
    bool queuedForOperations;

    std::vector<Collider *> collidersToAdd;
    std::vector<Collider *> collidersToRemove;
    std::vector<Collider *> collidersToSync;
};
