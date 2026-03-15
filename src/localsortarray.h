#pragma once

#include <algorithm>
#include <vector>
#include <unordered_set>
#include "collider.h"
// #include "onswapcallback.h"

const int MAX_SIZE = 32;

class LocalSortArray
{
public:
    LocalSortArray();
    LocalSortArray(LocalSortArray *other);
    ~LocalSortArray();

    bool add(BoundingRadiusProjection *element);
    BoundingRadiusProjection *pop();
    BoundingRadiusProjection *remove(size_t index);
    bool remove(BoundingRadiusProjection *element);
    void sort();
    BoundingRadiusProjection *get(size_t index);
    BoundingRadiusProjection *operator[](size_t index);
    BoundingRadiusProjection *getMax();
    BoundingRadiusProjection *getMin();
    size_t getSize() const;
    BoundingRadiusProjection **getArray();
    void clear();
    void addCheckpoint(Collider *element);
    void removeCheckpoint(Collider *element);
    std::vector<Collider *> *getCheckpoint();

    size_t findBinarySearchIndex(BoundingRadiusProjection *element);
    size_t searchAround(size_t index, BoundingRadiusProjection *element);

    void setIsDirty(bool dirty);
    bool getIsDirty() const;

private:
    size_t binarySearch(BoundingRadiusProjection *element);

    size_t size;
    BoundingRadiusProjection *array[MAX_SIZE];

    // The ‘checkpoints’ set contains the set of object id’s which
    // overlap with the ‘trailing edge’ of the chunk (AABBs whose min-
    // ima is within this chunk or one to the left, but whose maxima is in
    // a chunk to the right
    std::vector<Collider *> checkpoint;
    bool isDirty;

    friend class SegmentedIntervalList;
};