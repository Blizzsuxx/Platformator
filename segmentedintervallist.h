#pragma once

#include <vector>
#include "localsortarray.h"

template <typename T>
class SegmentedIntervalList
{
public:
    SegmentedIntervalList();
    ~SegmentedIntervalList();

    void add(T& element);
    void remove(T& element);
    void clear();

private:
    std::vector<LocalSortArray<T>*> arrays;
    size_t size;
};