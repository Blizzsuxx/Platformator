#pragma once

#include <algorithm>
#include <vector>
#include <unordered_set>
#include "collider.h"

const int MAX_SIZE = 32;

class SwapCallback;
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
    void sort(SwapCallback *callback);
    void sortFromIndex(size_t index, SwapCallback *callback);

    BoundingRadiusProjectionProxy *get(size_t index);
    BoundingRadiusProjectionProxy *operator[](size_t index);
    BoundingRadiusProjectionProxy *getMax();
    BoundingRadiusProjectionProxy *getMin();
    size_t getSize() const;
    BoundingRadiusProjectionProxy **getArray();
    void clear();
    void addCheckpoint(Collider *element);
    void removeCheckpoint(Collider *element);
    std::unordered_set<Collider *> *getCheckpoints();

    size_t find(BoundingRadiusProjectionProxy *element);

    void setIsDirty(bool dirty);
    bool getIsDirty() const;

    LocalSortArray *getLeftChunk() const;
    LocalSortArray *getRightChunk() const;
    void setLeftChunk(LocalSortArray *leftChunk);
    void setRightChunk(LocalSortArray *rightChunk);

    SegmentedIntervalList *getOwner() const;
    void setOwner(SegmentedIntervalList *owner);

private:
    size_t binarySearch(BoundingRadiusProjectionProxy *element);
    size_t searchAround(size_t index, BoundingRadiusProjectionProxy *element);
    void addCheckpointInternal(Collider *element);
    std::size_t removeCheckpointInternal(Collider *element);

    size_t size;
    BoundingRadiusProjectionProxy *array[MAX_SIZE];

    // The ‘checkpoints’ set contains the set of object id’s which
    // overlap with the ‘trailing edge’ of the chunk (AABBs whose min-
    // ima is within this chunk or one to the left, but whose maxima is in
    // a chunk to the right

    // Needed even in an event driven model (let's say (Amin, Bmax), (Cmin, Dmax), (Cmax, Amax),
    // and we want to add Emin and Emax at Cmin Dmax place, we need to check checkpoints for A (and we cannot see it with events))
    std::unordered_set<Collider *> checkpoints;
    std::unordered_set<Collider *> checkpointCache;

    bool isDirty;
    LocalSortArray *leftChunk;
    LocalSortArray *rightChunk;
    SegmentedIntervalList *owner;

    friend class SegmentedIntervalList;
};