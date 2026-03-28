#include "gridcell.h"

GridCell::GridCell(GridCellKey key, Grid *owner)
    : aabb(owner), key(key)
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