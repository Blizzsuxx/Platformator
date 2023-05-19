#pragma once

#include <algorithm>
#include <vector>
#include "collider.h"

const size_t MAX_SIZE = 32;

class LocalSortArray
{
public:
    LocalSortArray();
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
    size_t getSize() const;
    BoundingRadiusProjection* *getArray();
    void clear();
    void addCheckpoint(Collider *element);
    void removeCheckpoint(Collider *element);
    void swap(size_t index1, LocalSortArray *array2, size_t index2);

private:
    size_t binarySearch(BoundingRadiusProjection *element);

    BoundingRadiusProjection *array[MAX_SIZE];
    std::vector<Collider*> checkpoint;
    size_t size;

    friend class SegmentedIntervalList;
};