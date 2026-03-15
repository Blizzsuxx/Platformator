#pragma once

#include <vector>
#include "localsortarray.h"

class SegmentedIntervalList
{
public:
    SegmentedIntervalList();
    ~SegmentedIntervalList();

    size_t getSize() const;
    std::vector<LocalSortArray *> *getChunks();
    void clear();
    void sort();
    void add(BoundingRadiusProjectionAxis *axis);
    void remove(BoundingRadiusProjectionAxis *axis);

private:
    size_t binarySearch(BoundingRadiusProjection *element);
    size_t binarySearch(LocalSortArray *array);

    size_t add(BoundingRadiusProjection *element);
    size_t add(BoundingRadiusProjection *element, size_t chunkIndex);
    LocalSortArray *remove(BoundingRadiusProjection *element);
    BoundingRadiusProjection *remove(size_t chunkIndex, size_t arrayIndex);
    void updateCheckpoint(size_t indexWhereItWasAdded, size_t indexWhereItWasRemoved, BoundingRadiusProjection *element);

    std::vector<LocalSortArray *> chunks;
    size_t size;
};