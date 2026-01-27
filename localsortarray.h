#pragma once

#include <algorithm>
#include <vector>
#include <unordered_set>
#include "collider.h"

const int MAX_SIZE = 32;

class LocalSortArray
{
public:
    LocalSortArray();
    LocalSortArray(LocalSortArray *other);
    ~LocalSortArray();

    bool add(BoundingRadiusProjection *element);
    bool addWithoutSort(BoundingRadiusProjection *element);
    BoundingRadiusProjection *pop();
    BoundingRadiusProjection *addAndPop(BoundingRadiusProjection *element);
    BoundingRadiusProjection *remove(size_t index);
    bool remove(BoundingRadiusProjection *element);
    void sort();
    void sort(size_t index);
    BoundingRadiusProjection *get(size_t index);
    BoundingRadiusProjection *operator[](size_t index);
    BoundingRadiusProjection *getMax();
    BoundingRadiusProjection *getMin();
    int getSize() const;
    BoundingRadiusProjection **getArray();
    void clear();
    void addCheckpoint(Collider *element);
    void removeCheckpoint(Collider *element);
    void swap(size_t index1, LocalSortArray *array2, size_t index2);
    std::unordered_set<Collider *> *getCheckpoint();

private:
    size_t binarySearch(BoundingRadiusProjection *element);

    size_t size;
    BoundingRadiusProjection *array[MAX_SIZE];
    std::unordered_set<Collider *> checkpoint;

    friend class SegmentedIntervalList;
};