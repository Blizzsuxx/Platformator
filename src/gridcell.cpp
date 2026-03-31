#include "gridcell.h"

GridCell::GridCell(GridCellKey key, Grid *owner)
    : aabb(owner), key(key), isDirty(false)
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

void GridCell::sort()
{
    aabb.sort();
}

AABB *GridCell::getAABB()
{
    return &aabb;
}

const GridCellKey &GridCell::getCellKey() const
{
    return key;
}

bool GridCell::getIsDirty() const
{
    return isDirty;
}

void GridCell::setIsDirty(bool newIsDirty)
{
    isDirty = newIsDirty;
}