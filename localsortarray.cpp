#include "localsortarray.h"

LocalSortArray::LocalSortArray()
    : size(0), array(), checkpoint()
{
}

LocalSortArray::LocalSortArray(LocalSortArray *other)
    : size(other->size / 2), array(), checkpoint(other->checkpoint)
{
    std::copy(other->array + size, other->array + MAX_SIZE, array);
    other->size = size;

    for (int i = size - 1; i >= 0; i--)
    {
        if (array[i]->getIsEnd())
        {
            other->addCheckpoint(array[i]->getCollider());
        }
        else
        {
            other->removeCheckpoint(array[i]->getCollider());
        }
    }
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
        size_t index = binarySearch(element);

        // insert the element at the index
        for (size_t i = size; i > index; i--)
        {
            array[i] = array[i - 1];
        }
        array[index] = element;
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

BoundingRadiusProjection *LocalSortArray::pop()
{
    BoundingRadiusProjection *removed = array[size - 1];
    size--;
    return removed;
}

BoundingRadiusProjection *LocalSortArray::addAndPop(BoundingRadiusProjection *element)
{
    BoundingRadiusProjection *removed = array[size - 1];
    size--;
    add(element);
    return removed;
}

BoundingRadiusProjection *LocalSortArray::remove(size_t index)
{
    if (index >= size)
    {
        return nullptr;
    }
    // remove the element at the specified index
    BoundingRadiusProjection *removed = array[index];

    for (size_t i = index; i < size - 1; i++)
    {
        array[i] = array[i + 1];
    }
    size--;

    return removed;
}

bool LocalSortArray::remove(BoundingRadiusProjection *element)
{
    // find the index of the element using binary search
    size_t index = binarySearch(element);

    if (index != (size_t)-1)
    {
        remove(index);
        return true;
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
        while (j != (size_t)-1 && *array[j] > *temp)
        {
            array[j + 1] = array[j];
            j--;
        }
        array[j + 1] = temp;
    }
}

void LocalSortArray::sort(size_t index)
{
    if (index >= size)
    {
        return;
    }

    BoundingRadiusProjection *value = array[index];

    // Move right if value is greater than the next element
    if (index + 1 < size && *value > *array[index + 1])
    {
        size_t j = index;
        while (j + 1 < size && *value > *array[j + 1])
        {
            array[j] = array[j + 1];
            j++;
        }
        array[j] = value;
    }
    // Move left if value is less than the previous element
    else if (index > 0 && *value < *array[index - 1])
    {
        size_t j = index;
        while (j > 0 && *value < *array[j - 1])
        {
            array[j] = array[j - 1];
            j--;
        }
        array[j] = value;
    }
}

BoundingRadiusProjection *LocalSortArray::get(size_t index)
{
    return array[index];
}

BoundingRadiusProjection *LocalSortArray::operator[](size_t index)
{
    return array[index];
}

BoundingRadiusProjection *LocalSortArray::getMax()
{
    return array[size - 1];
}

BoundingRadiusProjection *LocalSortArray::getMin()
{
    return array[0];
}

int LocalSortArray::getSize() const
{
    return size;
}

BoundingRadiusProjection **LocalSortArray::getArray()
{
    return array;
}

void LocalSortArray::clear()
{
    size = 0;
    checkpoint.clear();
}

void LocalSortArray::addCheckpoint(Collider *collider)
{
    checkpoint.insert(collider);
}

void LocalSortArray::removeCheckpoint(Collider *collider)
{
    checkpoint.erase(collider);
}

size_t LocalSortArray::binarySearch(BoundingRadiusProjection *element)
{
    size_t low = 0;
    size_t high = size - 1;
    size_t mid = 0;
    BoundingRadiusProjection value = *element;

    while (low <= high)
    {
        mid = (low + high) / 2;

        if (value == *array[mid])
        {
            return mid;
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

    return low;
}

void LocalSortArray::swap(size_t index1, LocalSortArray *array2, size_t index2)
{
    BoundingRadiusProjection *temp = array[index1];
    array[index1] = array2->array[index2];
    array2->array[index2] = temp;
}

std::unordered_set<Collider *> *LocalSortArray::getCheckpoint()
{
    return &checkpoint;
}
