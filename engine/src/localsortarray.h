#pragma once

#include <algorithm>
#include <unordered_set>
#include <vector>
#include "platformator/collider.h"
#include "constants.h"

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
    std::unordered_set<Collider *> *getCheckpoints()
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
    size_t binarySearch(BoundingRadiusProjectionProxy *element);
    void addCheckpointInternal(Collider *element);
    std::size_t removeCheckpointInternal(Collider *element);

    size_t size;
    BoundingRadiusProjectionProxy *array[MAX_CHUNK_SIZE];

    // The ‘checkpoints’ set contains the set of object id’s which
    // overlap with the ‘trailing edge’ of the chunk (AABBs whose min-
    // ima is within this chunk or one to the left, but whose maxima is in
    // a chunk to the right

    // Needed even in an event driven model (let's say (Amin, Bmax), (Cmin, Dmax), (Cmax, Amax),
    // and we want to add Emin and Emax at Cmin Dmax place, we need to check checkpoints for A (and we cannot see it
    // with events))
    std::unordered_set<Collider *> checkpoints;
    std::unordered_set<Collider *> checkpointCache;

    LocalSortArray *leftChunk;
    LocalSortArray *rightChunk;
    SegmentedIntervalList *owner;

    friend class SegmentedIntervalList;
};