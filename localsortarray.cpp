#include "localsortarray.h"

template <class T>
LocalSortArray<T>::LocalSortArray()
    : size(0)
{
}

template <class T>
LocalSortArray<T>::~LocalSortArray()
{
}

template <class T>
bool LocalSortArray<T>::add(T& element)
{
    // find the index where the element should be inserted using binary search
    if (size == MAX_SIZE)
    {
        return false;
    }
    else if (size == 0)
    {
        array[0] = element;
        size++;
        return true;
    }
    else
    {
        size_t low = 0;
        size_t high = size - 1;
        size_t mid = 0;

        while (low <= high)
        {
            mid = (low + high) / 2;

            // check if the element belongs in the middle
            if (element >= array[mid] && element <= array[mid + 1])
            {
                low = mid;
                break;
            }
            else if (element < array[mid])
            {
                high = mid - 1;
            }
            else if (element > array[mid])
            {
                low = mid + 1;
            }
        }
        
        // insert the element at the index
        for (size_t i = size; i > low; i--)
        {
            array[i] = array[i - 1];
        }
        array[low] = element;
        size++;
        return true;
    }
}

template <class T>
bool LocalSortArray<T>::add(T* element)
{
    return add(*element);
}

template <class T>
T LocalSortArray<T>::pop()
{
    T removed = array[size - 1];
    size--;
    return removed;
}

template <class T>
T LocalSortArray<T>::addAndPop(T& element)
{
    T removed = array[size - 1];
    size--;
    add(element);
    return removed;
}

template <class T>
bool LocalSortArray<T>::remove(size_t index)
{
    // remove the element at the specified index
    if (index < 0 || index >= size)
    {
        return false;
    }
    else
    {
        for (size_t i = index; i < size - 1; i++)
        {
            array[i] = array[i + 1];
        }
        size--;
        return true;
    }
}

template <class T>
void LocalSortArray<T>::sort()
{
    // sort the array with insertion sort, sort from lowest to highest
    for (size_t i = 1; i < size; i++)
    {
        T temp = array[i];
        size_t j = i - 1;
        while (j >= 0 && array[j] > temp)
        {
            array[j + 1] = array[j];
            j--;
        }
        array[j + 1] = temp;
    }
}

template <class T>
void LocalSortArray<T>::sort(size_t index)
{
    // sort only the element at the specified index, assume all other elements are sorted
    if (index < 0 || index >= size)
    {
        return;
    }

    T value = array[index];
    
    if (value > array[index + 1])
    {
        for (size_t i = index + 1; i < size; i++)
        {
            T temp = array[i];
            size_t j = i - 1;
            while (j >= 0 && array[j] > temp)
            {
                array[j + 1] = array[j];
                j--;
            }
            array[j + 1] = temp;
        }
    }
    else if (value < array[index - 1])
    {
        for (size_t i = index - 1; i >= 0; i--)
        {
            T temp = array[i];
            size_t j = i + 1;
            while (j < size && array[j] < temp)
            {
                array[j - 1] = array[j];
                j++;
            }
            array[j - 1] = temp;
        }
    }
}

template <class T>
T& LocalSortArray<T>::get(size_t index)
{
    return array[index];
}

template <class T>
T& LocalSortArray<T>::operator[](size_t index)
{
    return array[index];
}

template <class T>
T& LocalSortArray<T>::getMax()
{
    return array[size - 1];
}

template <class T>
size_t LocalSortArray<T>::getSize() const
{
    return size;
}

template <class T>
T* LocalSortArray<T>::getArray()
{
    return array;
}

template <class T>
void LocalSortArray<T>::clear()
{
    size = 0;
}
