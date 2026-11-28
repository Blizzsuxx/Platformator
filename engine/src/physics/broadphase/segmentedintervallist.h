#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "physics/broadphase/localsortarray.h"

class AABB;

class SegmentedIntervalList
{
  public:
    SegmentedIntervalList(AABB *owner, bool isPrimary);
    ~SegmentedIntervalList();

    void clear();
    void resetToEmpty();
    void add(platformator_detail::ProjectionAxisBinding *binding);
    void addBulkEmpty(const std::vector<platformator_detail::ProjectionProxy *> &sortedProxies);
    void remove(platformator_detail::ProjectionAxisBinding *binding);
    void repairProjection(platformator_detail::ProjectionProxy *projection);

    bool getIsPrimary() const
    {
        return isPrimary;
    }
    bool getIsEmpty() const
    {
        return chunks.size() == 1 && chunks[0]->getSize() == 0;
    }
    size_t getChunkCount() const
    {
        return chunks.size();
    }

    template <typename Visitor>
    void forEachOverlappingInterval(float minCoord, float maxCoord, Visitor &&visitor) const
    {
        if (chunks.empty() || (chunks.size() == 1 && chunks[0]->getSize() == 0))
            return;

        size_t low = 0;
        size_t high = chunks.size();
        while (low < high)
        {
            const size_t mid = low + (high - low) / 2;
            if (chunks[mid]->getSize() == 0 || chunks[mid]->getMaxPosition() < minCoord)
            {
                low = mid + 1;
            }
            else
            {
                high = mid;
            }
        }
        if (low >= chunks.size())
            return;

        LocalSortArray *startChunk = chunks[low];
        if (LocalSortArray *leftChunk = startChunk->getLeftChunk())
        {
            for (Collider *collider : *leftChunk->getCheckpoints())
            {
                if (collider != nullptr)
                    visitor(collider);
            }
        }

        for (LocalSortArray *chunk = startChunk;
             chunk != nullptr && chunk->getSize() > 0 && chunk->getMinPosition() <= maxCoord;
             chunk = chunk->getRightChunk())
        {
            platformator_detail::ProjectionProxy **arr = chunk->getArray();
            const size_t sz = chunk->getSize();
            for (size_t i = 0; i < sz; ++i)
            {
                if (chunk->getPosition(i) > maxCoord)
                    return;
                if (!chunk->getIsMaxima(i))
                {
                    if (Collider *collider = arr[i]->getCollider())
                        visitor(collider);
                }
            }
        }
    }

  private:
    static constexpr size_t BULK_CHUNK_FILL = 24;

    template <typename EmitFn>
    void processProjectionCollisions(platformator_detail::ProjectionProxy *lowerProjection, size_t lowerIndexInsideChunk,
                                     platformator_detail::ProjectionProxy *upperProjection, size_t upperIndexInsideChunk,
                                     EmitFn &&emit);

    size_t binarySearch(platformator_detail::ProjectionProxy *element) const noexcept;
    std::pair<LocalSortArray *, size_t> find(platformator_detail::ProjectionProxy *element);

    void add(platformator_detail::ProjectionProxy *element);
    void add(platformator_detail::ProjectionProxy *element, size_t chunkIndex);
    void remove(platformator_detail::ProjectionProxy *element);
    void remove(LocalSortArray *array, size_t index);
    std::pair<LocalSortArray *, size_t> getPreviousIndex(LocalSortArray *chunk, size_t index) const;
    std::pair<LocalSortArray *, size_t> getNextIndex(LocalSortArray *chunk, size_t index) const;

    void repairProjectionFromIndexInternal(platformator_detail::ProjectionProxy *projection, LocalSortArray *chunk,
                                           size_t arrayIndex);
    void swap(platformator_detail::ProjectionProxy *leftProxy, size_t leftProxyIndex,
              platformator_detail::ProjectionProxy *rightProxy, size_t rightProxyIndex);

    LocalSortArray *allocateEmptyChunk();
    LocalSortArray *allocateSplitChunk(LocalSortArray *other);
    void recycleChunk(LocalSortArray *chunk);
    void eraseChunkFromList(LocalSortArray *target);

    std::vector<LocalSortArray *> chunks;
    std::vector<LocalSortArray *> freeChunks;
    AABB *owner;

    // Only the primary axis maintains checkpoints and emits overlap transitions on add/remove.
    bool isPrimary;
};
