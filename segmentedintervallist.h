#pragma once

#include <vector>
#include "localsortarray.h"

class SegmentedIntervalList
{
public:
    SegmentedIntervalList();
    ~SegmentedIntervalList();

    // void add(Collider *element);
    // void remove(Collider *element);
    size_t getSize() const;
    std::vector<LocalSortArray *> *getChunks();
    void clear();
    void sort();
    void sort(size_t index);
    void add(BoundingRadiusProjectionAxis *axis);
    void remove(BoundingRadiusProjectionAxis *axis);

private:
    size_t binarySearch(BoundingRadiusProjection *element);
    size_t add(BoundingRadiusProjection *element);
    size_t add(BoundingRadiusProjection *element, size_t chunkIndex);
    size_t remove(BoundingRadiusProjection *element);
    BoundingRadiusProjection *remove(size_t chunkIndex, size_t arrayIndex);
    void swap(BoundingRadiusProjection *element1, BoundingRadiusProjection *element2);
    void swap(size_t chunkIndex1, size_t arrayIndex1, size_t chunkIndex2, size_t arrayIndex2);
    void updateCheckpoint(size_t chunkIndex1, size_t arrayIndex1, size_t chunkIndex2);
    void updateCheckpoint(size_t chunkIndex, BoundingRadiusProjection *element, size_t chunkIndex2);

    std::vector<LocalSortArray *> chunks;
    size_t size;
};