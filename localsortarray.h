#pragma once

#include <algorithm>

const size_t MAX_SIZE = 32;

template <class T>
class LocalSortArray
{
public:
    LocalSortArray();
    ~LocalSortArray();

    bool add(T& element);
    bool add(T* element);
    T pop();
    T addAndPop(T& element);
    bool remove(size_t index);
    void sort();
    void sort(size_t index);
    T& get(size_t index);
    T& operator[](size_t index);
    T& getMax();
    size_t getSize() const;
    T* getArray();
    void clear();

private:
    T array[MAX_SIZE];
    size_t size;
};