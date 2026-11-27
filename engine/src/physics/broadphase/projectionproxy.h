#pragma once

#include <cstddef>
#include <cstdint>

#include "core/flathash.h"
#include "platformator/collider.h"

class LocalSortArray;

namespace platformator_detail
{
inline bool isProjectionKeyLess(float posA, bool maxA, float posB, bool maxB) noexcept
{
    return posA < posB || (posA == posB && !maxA && maxB);
}

inline bool isProjectionKeyGreater(float posA, bool maxA, float posB, bool maxB) noexcept
{
    return posA > posB || (posA == posB && maxA && !maxB);
}

struct CanonicalColliderPairKey
{
    Collider *a = nullptr;
    Collider *b = nullptr;

    constexpr CanonicalColliderPairKey() noexcept = default;
    constexpr CanonicalColliderPairKey(Collider *first, Collider *second) noexcept
        : a(first <= second ? first : second), b(first <= second ? second : first)
    {
    }

    bool operator==(const CanonicalColliderPairKey &) const noexcept = default;

    struct IsEmpty
    {
        bool operator()(const CanonicalColliderPairKey &pair) const noexcept
        {
            return pair.a == nullptr;
        }
    };

    struct Hash
    {
        size_t operator()(const CanonicalColliderPairKey &pair) const noexcept
        {
            return hashPointerPair(pair.a, pair.b);
        }
    };
};

class ProjectionProxy
{
  public:
    static constexpr uint8_t INVALID_CHUNK_INDEX = 0xFFu;

    ProjectionProxy(Collider *collider, bool isMaxima, bool isYAxis) noexcept
        : collider(collider), chunk(nullptr), cachedProjectedPosition(readCurrentPosition(collider, isMaxima, isYAxis)),
          chunkIndex(INVALID_CHUNK_INDEX), isMaxima(isMaxima), isYAxis(isYAxis)
    {
    }

    LocalSortArray *getChunk() const noexcept
    {
        return chunk;
    }
    size_t getChunkIndex() const noexcept
    {
        return chunkIndex == INVALID_CHUNK_INDEX ? SIZE_MAX : static_cast<size_t>(chunkIndex);
    }

    void setChunk(LocalSortArray *newChunk) noexcept
    {
        chunk = newChunk;
        if (newChunk == nullptr)
        {
            chunkIndex = INVALID_CHUNK_INDEX;
        }
    }
    void setChunkIndex(size_t index) noexcept
    {
        chunkIndex = index == SIZE_MAX ? INVALID_CHUNK_INDEX : static_cast<uint8_t>(index);
    }
    void updateCachedProjectedPosition() noexcept
    {
        cachedProjectedPosition = getProjectedPosition();
    }

    Collider *getCollider() const noexcept
    {
        return collider;
    }
    float getProjectedPosition() const noexcept
    {
        return readCurrentPosition(collider, isMaxima, isYAxis);
    }
    bool getIsMaxima() const noexcept
    {
        return isMaxima;
    }
    float getCachedProjectedPosition() const noexcept
    {
        return cachedProjectedPosition;
    }

  private:
    static float readCurrentPosition(const Collider *collider, bool maxima, bool yAxis) noexcept
    {
        if (yAxis)
        {
            return maxima ? collider->getFatMaxY() : collider->getFatMinY();
        }
        return maxima ? collider->getFatMaxX() : collider->getFatMinX();
    }

    Collider *collider;
    LocalSortArray *chunk;
    float cachedProjectedPosition;
    uint8_t chunkIndex;
    bool isMaxima;
    bool isYAxis;
};

struct ProjectionAxisBinding
{
    ProjectionProxy minProxy;
    ProjectionProxy maxProxy;

    ProjectionAxisBinding(Collider *collider, bool isYAxis) noexcept
        : minProxy(collider, false, isYAxis), maxProxy(collider, true, isYAxis)
    {
    }
};
} // namespace platformator_detail
