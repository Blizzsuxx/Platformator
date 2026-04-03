#pragma once

#include "aabb.h"
#include "gridtypes.h"

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

private:
    AABB aabb;
    GridCellKey key;
};
