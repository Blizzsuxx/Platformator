#pragma once

#include <vector>
#include "localsortarray.h"

class SegmentedIntervalList
{
public:
    SegmentedIntervalList();
    ~SegmentedIntervalList();

    void add(Collider* element, size_t index);
    void remove(Collider* element, size_t index);
    void clear();
    void sort();
    void sort(size_t index);

private:
    std::vector<LocalSortArray*> arrays;
    size_t size;

    size_t binarySearch(BoundingRadiusProjection* element);
    size_t add(BoundingRadiusProjection* element);
    size_t remove(BoundingRadiusProjection* element);
};