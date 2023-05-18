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

    bool add(BoundingRadiusProjection* element);
    bool addWithoutSort(BoundingRadiusProjection* element);
    BoundingRadiusProjection* pop();
    BoundingRadiusProjection* addAndPop(BoundingRadiusProjection* element);
    void remove(size_t index);
    bool remove(BoundingRadiusProjection* element);
    void sort();
    void sort(size_t index);
    BoundingRadiusProjection* get(size_t index);
    BoundingRadiusProjection* operator[](size_t index);
    BoundingRadiusProjection* getMax();
    size_t getSize() const;
    BoundingRadiusProjection** getArray();
    void clear();
    void addCheckpoint(Collider* element);
    void removeCheckpoint(Collider* element);

private:
    BoundingRadiusProjection* array[MAX_SIZE];
    std::vector<Collider*> checkpoint;
    size_t size;
};