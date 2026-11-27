#pragma once

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>

struct GridCellKey
{
    int x = 0;
    int y = 0;

    constexpr GridCellKey() noexcept = default;
    constexpr GridCellKey(int x, int y) noexcept : x(x), y(y)
    {
    }

    bool operator==(const GridCellKey &other) const
    {
        return x == other.x && y == other.y;
    }

    struct Hash
    {
        std::size_t operator()(const GridCellKey &key) const noexcept
        {
            uint64_t z = (static_cast<uint64_t>(static_cast<uint32_t>(key.x)) << 32) |
                         static_cast<uint64_t>(static_cast<uint32_t>(key.y));
            z ^= z >> 30;
            z *= 0xbf58476d1ce4e5b9ULL;
            z ^= z >> 27;
            z *= 0x94d049bb133111ebULL;
            z ^= z >> 31;
            return static_cast<std::size_t>(z);
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

    template <typename Fn>
    void forEachSame(const GridCellRange &other, Fn &&fn) const
    {
        GridCellRange overlap = intersection(other);
        if (!overlap.getIsValid())
        {
            return;
        }

        overlap.forEachCell(std::forward<Fn>(fn));
    }
};