#pragma once

#include "aabb.h"
#include "gridtypes.h"
#include <atomic>
#include <oneapi/tbb.h>

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
    const GridCellKey &getCellKey() const;
    bool markQueuedForEmpty();
    void clearQueuedForEmpty();
    bool markQueuedForOperations();
    void clearQueuedForOperations();
    void queueRemoveBinding(AABB::ColliderBinding *binding);
    void queueAddCollider(Collider *collider);
    void queueSyncBinding(AABB::ColliderBinding *binding);
    void flushPendingOperations();

private:
    AABB aabb;
    GridCellKey key;
    std::atomic_bool queuedForEmpty;
    std::atomic_bool queuedForOperations;

    tbb::concurrent_vector<Collider *> collidersToAdd;
    tbb::concurrent_vector<AABB::ColliderBinding *> bindingsToRemove;
    tbb::concurrent_vector<AABB::ColliderBinding *> bindingsToSync;
};
