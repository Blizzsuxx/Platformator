#pragma once

#include <cstdint>

#include "core/constants.h"
#include "core/flatset.h"
#include "physics/broadphase/projectionproxy.h"

class SegmentedIntervalList;

class LocalSortArray
{
  public:
    LocalSortArray(SegmentedIntervalList *owner);
    LocalSortArray(LocalSortArray *other);
    ~LocalSortArray() = default;

    size_t add(platformator_detail::ProjectionProxy *element);
    platformator_detail::ProjectionProxy *remove(size_t index);

    platformator_detail::ProjectionProxy *get(size_t index)
    {
        return array[index];
    }
    float getPosition(size_t index) const noexcept
    {
        return positions[index];
    }
    bool getIsMaxima(size_t index) const noexcept
    {
        return isMaxima[index] != 0;
    }
    float getMinPosition() const noexcept
    {
        return positions[0];
    }
    float getMaxPosition() const noexcept
    {
        return positions[size - 1];
    }
    bool getMaxIsMaxima() const noexcept
    {
        return isMaxima[size - 1] != 0;
    }
    void setPosition(size_t index, float pos) noexcept
    {
        positions[index] = pos;
    }
    void setSlot(size_t index, platformator_detail::ProjectionProxy *proxy, float pos, bool maxFlag) noexcept
    {
        array[index] = proxy;
        positions[index] = pos;
        isMaxima[index] = maxFlag ? 1 : 0;
    }
    void appendSorted(platformator_detail::ProjectionProxy *proxy) noexcept
    {
        const size_t idx = size++;
        array[idx] = proxy;
        positions[idx] = proxy->getCachedProjectedPosition();
        isMaxima[idx] = proxy->getIsMaxima() ? 1 : 0;
        proxy->setChunk(this);
        proxy->setChunkIndex(idx);
    }
    size_t getSize() const
    {
        return size;
    }
    platformator_detail::ProjectionProxy **getArray()
    {
        return array;
    }
    void clear()
    {
        size = 0;
        checkpoints.clear();
        checkpointCache.clear();
    }
    void addCheckpoint(Collider *element);
    void removeCheckpoint(Collider *element);
    FlatPointerSet<Collider> *getCheckpoints()
    {
        return &checkpoints;
    }

    size_t find(platformator_detail::ProjectionProxy *element)
    {
        const size_t cachedIndex = element->getChunkIndex();
        if (cachedIndex < size && array[cachedIndex] == element)
        {
            return cachedIndex;
        }
        return logInvalidFind();
    }

    LocalSortArray *getLeftChunk() const
    {
        return leftChunk;
    }
    LocalSortArray *getRightChunk() const
    {
        return rightChunk;
    }
    void setLeftChunk(LocalSortArray *leftChunk)
    {
        this->leftChunk = leftChunk;
    }
    void setRightChunk(LocalSortArray *rightChunk)
    {
        this->rightChunk = rightChunk;
    }
    void setOwner(SegmentedIntervalList *owner)
    {
        this->owner = owner;
    }

    void initFromSplit(LocalSortArray *other);
    LocalSortArray *tryMergeWithLeftChunk();
    LocalSortArray *tryMergeWithRightChunk();

  private:
    size_t logInvalidFind();
    size_t binarySearch(float pos, bool maxFlag) const noexcept;

    // Cache-line-aligned SoA keys eliminate pointer chasing during binary search and interval repair.
    alignas(64) float positions[MAX_CHUNK_SIZE];
    uint8_t isMaxima[MAX_CHUNK_SIZE];
    platformator_detail::ProjectionProxy *array[MAX_CHUNK_SIZE];
    size_t size;
    size_t chunkListIndex = SIZE_MAX;

    // Checkpoints store colliders overlapping the trailing edge of the chunk
    // (minima within this chunk or left, maxima in a chunk to the right).
    FlatPointerSet<Collider> checkpoints;
    FlatPointerSet<Collider> checkpointCache;

    LocalSortArray *leftChunk;
    LocalSortArray *rightChunk;
    SegmentedIntervalList *owner;

    friend class SegmentedIntervalList;
};
