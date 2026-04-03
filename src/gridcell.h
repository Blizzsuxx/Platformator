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

struct GridCellRange
{
    GridCellRange(int minX, int maxX, int minY, int maxY) : minX(minX), maxX(maxX), minY(minY), maxY(maxY) {}
    GridCellRange() : minX(INT_MAX), maxX(INT_MIN), minY(INT_MAX), maxY(INT_MIN) {}

    int minX;
    int maxX;
    int minY;
    int maxY;

    bool getIsValid() const
    {
        return minX != INT_MAX && maxX != INT_MIN && minY != INT_MAX && maxY != INT_MIN;
    }

    bool operator==(const GridCellRange &other) const
    {
        return minX == other.minX && maxX == other.maxX && minY == other.minY && maxY == other.maxY;
    }

    std::vector<GridCellKey> difference(const GridCellRange &other) const
    {
        std::vector<GridCellKey> differences;

        for (int x = minX; x <= maxX; ++x)
        {
            for (int y = minY; y <= maxY; ++y)
            {
                if (x < other.minX || x > other.maxX || y < other.minY || y > other.maxY)
                {
                    differences.emplace_back(x, y);
                }
            }
        }

        return differences;
    }
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

private:
    AABB aabb;
    GridCellKey key;
};
