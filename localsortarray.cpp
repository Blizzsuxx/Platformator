#include "localsortarray.h"

LocalSortArray::LocalSortArray()
    : size(0), checkpoint()
{
}

LocalSortArray::~LocalSortArray()
{
}

bool LocalSortArray::add(BoundingRadiusProjection *element)
{
    // find the index where the element should be inserted using binary search
    if (size == MAX_SIZE)
    {
        return false;
    }
    else
    {
        size_t low = 0;
        size_t high = size - 1;
        size_t mid = 0;

        BoundingRadiusProjection value = *element;

        while (low <= high)
        {
            mid = (low + high) / 2;

            // check if the element belongs in the middle
            if (value >= *array[mid] && value <= *array[mid + 1])
            {
                low = mid;
                break;
            }
            else if (value < *array[mid])
            {
                high = mid - 1;
            }
            else if (value > *array[mid])
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

bool LocalSortArray::addWithoutSort(BoundingRadiusProjection *element)
{
    if (size == MAX_SIZE)
    {
        return false;
    }
    else
    {
        array[size] = element;
        size++;
        return true;
    }
}

BoundingRadiusProjection* LocalSortArray::pop()
{
    BoundingRadiusProjection *removed = array[size - 1];
    size--;
    return removed;
}

BoundingRadiusProjection* LocalSortArray::addAndPop(BoundingRadiusProjection *element)
{
    BoundingRadiusProjection *removed = array[size - 1];
    size--;
    add(element);
    return removed;
}

void LocalSortArray::remove(size_t index)
{
    // remove the element at the specified index
    for (size_t i = index; i < size - 1; i++)
    {
        array[i] = array[i + 1];
    }
    size--;
}

bool LocalSortArray::remove(BoundingRadiusProjection *element)
{
    // find the index of the element using binary search
    size_t low = 0;
    size_t high = size - 1;
    size_t mid = 0;
    BoundingRadiusProjection value = *element;

    while (low <= high)
    {
        mid = (low + high) / 2;

        if (value == *array[mid])
        {
            remove(mid);
            return true;
        }
        else if (value < *array[mid])
        {
            high = mid - 1;
        }
        else if (value > *array[mid])
        {
            low = mid + 1;
        }
    }

    return false;
}

void LocalSortArray::sort()
{
    // sort the array with insertion sort, sort from lowest to highest
    for (size_t i = 1; i < size; i++)
    {
        BoundingRadiusProjection *temp = array[i];
        size_t j = i - 1;
        while (j >= 0 && *array[j] > *temp)
        {
            array[j + 1] = array[j];
            j--;
        }
        array[j + 1] = temp;
    }
}

void LocalSortArray::sort(size_t index)
{
    // sort only the element at the specified index, assume all other elements are sorted
    if (index < 0 || index >= size)
    {
        return;
    }

    BoundingRadiusProjection *value = array[index];

    if (*value > *array[index + 1])
    {
        for (size_t i = index + 1; i < size; i++)
        {
            BoundingRadiusProjection *temp = array[i];
            size_t j = i - 1;
            while (j >= 0 && *array[j] > *temp)
            {
                array[j + 1] = array[j];
                j--;
            }
            array[j + 1] = temp;
        }
    }
    else if (*value < *array[index - 1])
    {
        for (size_t i = index - 1; i >= 0; i--)
        {
            BoundingRadiusProjection *temp = array[i];
            size_t j = i + 1;
            while (j < size && *array[j] < *temp)
            {
                array[j - 1] = array[j];
                j++;
            }
            array[j - 1] = temp;
        }
    }
}

BoundingRadiusProjection* LocalSortArray::get(size_t index)
{
    return array[index];
}

BoundingRadiusProjection* LocalSortArray::operator[](size_t index)
{
    return array[index];
}

BoundingRadiusProjection* LocalSortArray::getMax()
{
    return array[size - 1];
}

size_t LocalSortArray::getSize() const
{
    return size;
}

BoundingRadiusProjection** LocalSortArray::getArray()
{
    return array;
}

void LocalSortArray::clear()
{
    size = 0;
    checkpoint.clear();
}

void LocalSortArray::addCheckpoint(Collider* collider)
{
    checkpoint.push_back(collider);
}

void LocalSortArray::removeCheckpoint(Collider* collider)
{
    checkpoint.erase(std::remove(checkpoint.begin(), checkpoint.end(), collider), checkpoint.end());
}