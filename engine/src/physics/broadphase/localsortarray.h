#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>
#include "core/flatset.h"
#include "platformator/collider.h"
#include "core/constants.h"

class SegmentedIntervalList;

class LocalSortArray
{
  public:
    LocalSortArray(SegmentedIntervalList *owner);
    LocalSortArray(LocalSortArray *other);
    ~LocalSortArray();

    size_t add(BoundingRadiusProjectionProxy *element);
    BoundingRadiusProjectionProxy *pop();
    BoundingRadiusProjectionProxy *remove(size_t index);
    size_t remove(BoundingRadiusProjectionProxy *element);

    BoundingRadiusProjectionProxy *get(size_t index)
    {
        return array[index];
    }
    BoundingRadiusProjectionProxy *operator[](size_t index)
    {
        return array[index];
    }
    BoundingRadiusProjectionProxy *getMax()
    {
        return array[size - 1];
    }
    BoundingRadiusProjectionProxy *getMin()
    {
        return array[0];
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
    void setSlot(size_t index, BoundingRadiusProjectionProxy *proxy, float pos, bool maxFlag) noexcept
    {
        array[index] = proxy;
        positions[index] = pos;
        isMaxima[index] = maxFlag ? 1 : 0;
    }
    void appendSorted(BoundingRadiusProjectionProxy *proxy) noexcept
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
    BoundingRadiusProjectionProxy **getArray()
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

    size_t find(BoundingRadiusProjectionProxy *element)
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

    SegmentedIntervalList *getOwner() const
    {
        return owner;
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
    void addCheckpointInternal(Collider *element);
    std::size_t removeCheckpointInternal(Collider *element);

    // Cache-line-aligned SoA keys eliminate pointer chasing during binary search and interval repair.
    alignas(64) float positions[MAX_CHUNK_SIZE];
    uint8_t isMaxima[MAX_CHUNK_SIZE];
    BoundingRadiusProjectionProxy *array[MAX_CHUNK_SIZE];
    size_t size;
    size_t chunkListIndex = SIZE_MAX;

    // The ‘checkpoints’ set contains the set of object id’s which
    // overlap with the ‘trailing edge’ of the chunk (AABBs whose min-
    // ima is within this chunk or one to the left, but whose maxima is in
    // a chunk to the right)
    FlatPointerSet<Collider> checkpoints;
    FlatPointerSet<Collider> checkpointCache;

    LocalSortArray *leftChunk;
    LocalSortArray *rightChunk;
    SegmentedIntervalList *owner;

    friend class SegmentedIntervalList;
};
