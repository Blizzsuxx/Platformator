#pragma once

#include "aabb.h"
#include "gridtypes.h"
#include <oneapi/tbb.h>

class Grid;

enum GridCellFlags : uint8_t
{
    None = 0,
    QueuedForEmpty = 1 << 0,
    QueuedForOperations = 1 << 1
};

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
    bool getIsQueuedForEmpty() const;
    void setIsQueuedForEmpty(bool isQueuedForEmpty);
    bool getIsQueuedForOperations() const;
    void setIsQueuedForOperations(bool isQueuedForOperations);
    void queueRemoveCollider(Collider *collider);
    void queueAddCollider(Collider *collider);
    void queueSyncCollider(Collider *collider);
    void flushPendingOperations();

private:
    AABB aabb;
    GridCellKey key;
    uint8_t flags;

    tbb::concurrent_vector<Collider *> collidersToAdd;
    tbb::concurrent_vector<Collider *> collidersToRemove;
    tbb::concurrent_vector<Collider *> collidersToSync;
};
