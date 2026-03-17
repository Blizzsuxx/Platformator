#pragma once

#include <vector>
#include "localsortarray.h"
#include "swapcallback.h"

class SegmentedIntervalList : public SwapCallback
{
public:
    SegmentedIntervalList();
    ~SegmentedIntervalList();

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
    void swapBoundaries(LocalSortArray *leftChunk, LocalSortArray *rightChunk);
    void sortChunkFromIndex(LocalSortArray *chunk, size_t arrayIndex);

    void swap(BoundingRadiusProjection *leftRadiusProjection, size_t leftRadiusProjectionIndex, BoundingRadiusProjection *rightRadiusProjection, size_t rightRadiusProjectionIndex) override;

    std::vector<LocalSortArray *> chunks;
    std::vector<LocalSortArray *> dirtyChunks;
};