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

    BoundingRadiusProjectionProxy *get(size_t index);
    BoundingRadiusProjectionProxy *operator[](size_t index);
    BoundingRadiusProjectionProxy *getMax();
    BoundingRadiusProjectionProxy *getMin();
    size_t getSize() const;
    // Owned dynamic payload; excludes allocator headers and the inline owner object.
    size_t getOwnedCapacityBytes() const;
    BoundingRadiusProjectionProxy **getArray();
    void clear();
    void addCheckpoint(void *element);
    void removeCheckpoint(void *element);
    std::unordered_set<void *> *getCheckpoints();

    size_t find(BoundingRadiusProjectionProxy *element);

    LocalSortArray *getLeftChunk() const;
    LocalSortArray *getRightChunk() const;
    void setLeftChunk(LocalSortArray *leftChunk);
    void setRightChunk(LocalSortArray *rightChunk);

    SegmentedIntervalList *getOwner() const;
    void setOwner(SegmentedIntervalList *owner);

    LocalSortArray *tryMergeWithLeftChunk();
    LocalSortArray *tryMergeWithRightChunk();

  private:
    size_t binarySearch(BoundingRadiusProjectionProxy *element);
    void addCheckpointInternal(void *element);
    std::size_t removeCheckpointInternal(void *element);

    size_t size;
    BoundingRadiusProjectionProxy *array[MAX_CHUNK_SIZE];

    // The ‘checkpoints’ set contains the set of object id’s which
    // overlap with the ‘trailing edge’ of the chunk (AABBs whose min-
    // ima is within this chunk or one to the left, but whose maxima is in
    // a chunk to the right

    // Needed even in an event driven model (let's say (Amin, Bmax), (Cmin, Dmax), (Cmax, Amax),
    // and we want to add Emin and Emax at Cmin Dmax place, we need to check checkpoints for A (and we cannot see it
    // with events))
    std::unordered_set<void *> checkpoints;
    std::unordered_set<void *> checkpointCache;

    LocalSortArray *leftChunk;
    LocalSortArray *rightChunk;
    SegmentedIntervalList *owner;

    friend class SegmentedIntervalList;
};
