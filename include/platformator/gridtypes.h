#pragma once

#include <algorithm>
#include <climits>
#include <cstddef>
#include <functional>
#include <utility>

struct GridCellKey
{
    int x;
    int y;

    GridCellKey(int x, int y) : x(x), y(y)
    {
    }

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
    GridCellRange(int minX, int maxX, int minY, int maxY) : minX(minX), maxX(maxX), minY(minY), maxY(maxY)
    {
    }

    GridCellRange() : minX(INT_MAX), maxX(INT_MIN), minY(INT_MAX), maxY(INT_MIN)
    {
    }

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

    bool operator!=(const GridCellRange &other) const
    {
        return !(*this == other);
    }

    bool intersects(const GridCellRange &other) const
    {
        if (!getIsValid() || !other.getIsValid())
        {
            return false;
        }

        return !(maxX < other.minX || other.maxX < minX || maxY < other.minY || other.maxY < minY);
    }

    GridCellRange intersection(const GridCellRange &other) const
    {
        if (!intersects(other))
        {
            return GridCellRange();
        }

        return GridCellRange(
            std::max(minX, other.minX),
            std::min(maxX, other.maxX),
            std::max(minY, other.minY),
            std::min(maxY, other.maxY));
    }

    template <typename Fn>
    void forEachCell(Fn &&fn) const
    {
        for (int x = minX; x <= maxX; ++x)
        {
            for (int y = minY; y <= maxY; ++y)
            {
                fn(GridCellKey(x, y));
            }
        }
    }

    template <typename Fn>
    void forEachDifference(const GridCellRange &other, Fn &&fn) const
    {
        if (!intersects(other))
        {
            forEachCell(std::forward<Fn>(fn));
            return;
        }

        GridCellRange overlap = intersection(other);

        auto emitBand = [&](int bandMinX, int bandMaxX, int bandMinY, int bandMaxY)
        {
            if (bandMinX > bandMaxX || bandMinY > bandMaxY)
            {
                return;
            }

            for (int x = bandMinX; x <= bandMaxX; ++x)
            {
                for (int y = bandMinY; y <= bandMaxY; ++y)
                {
                    fn(GridCellKey(x, y));
                }
            }
        };

        emitBand(minX, overlap.minX - 1, minY, maxY);
        emitBand(overlap.maxX + 1, maxX, minY, maxY);
        emitBand(overlap.minX, overlap.maxX, minY, overlap.minY - 1);
        emitBand(overlap.minX, overlap.maxX, overlap.maxY + 1, maxY);
    }
};