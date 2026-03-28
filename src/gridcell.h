#pragma once

#include "aabb.h"

class Grid;

struct GridCellKey
{
    int x;
    int y;

    GridCellKey(int x, int y) : x(x), y(y) {}

    bool operator==(const GridCellKey &other) const
    {
        return x == other.x && y == other.y;
    }

    struct Hash
    {
        std::size_t operator()(const GridCellKey &key) const
        {
            size_t h1 = std::hash<int>()(key.x);
            size_t h2 = std::hash<int>()(key.y);

            return h1 ^ (h2 << 1);
        }
    };
};

class GridCell
{
public:
    GridCell(GridCellKey key, Grid *owner);
    ~GridCell();

    void addCollider(Collider *collider);
    void removeCollider(Collider *collider);
    AABB *getAABB();
    const GridCellKey &getCellKey() const;

private:
    AABB aabb;
    GridCellKey key;
};
