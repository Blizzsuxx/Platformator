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
    size_t index = findBinarySearchIndex(element);

    if (index < size && array[index] == element)
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
        while (j != static_cast<size_t>(-1) && *array[j] > *temp)
        {
            // // temp is moving LEFT past array[j]
            // // That means temp's value decreased (or array[j]'s increased)
            // if (temp->getCollider() != array[j]->getCollider())
            // {
            //     callback.onSwap(temp, array[j]);
            // }
            array[j + 1] = array[j];
            j--;
        }
        array[j + 1] = temp;
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

size_t LocalSortArray::getSize() const
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
    size_t high = size;

    while (low < high)
    {
        size_t mid = (low + high) / 2;

        if (*array[mid] == *element)
        {
            return mid;
        }
        else if (*array[mid] < *element)
        {
            low = mid + 1;
        }
        else
        {
            high = mid;
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

size_t LocalSortArray::findBinarySearchIndex(BoundingRadiusProjection *element)
{
    size_t low = 0;
    size_t high = size;

    while (low < high)
    {
        size_t mid = (low + high) / 2;

        if (array[mid] == element)
        {
            return mid;
        }
        if (*array[mid] == *element)
        {
            return searchAround(mid, element);
        }
        else if (*array[mid] < *element)
        {
            low = mid + 1;
        }
        else
        {
            high = mid;
        }
    }

    return low;
}

size_t LocalSortArray::searchAround(size_t index, BoundingRadiusProjection *element)
{
    // Search left
    for (size_t i = index; i != static_cast<size_t>(-1); i--)
    {
        if (array[i] == element)
        {
            return i;
        }
        if (*array[i] != *element)
        {
            break;
        }
    }

    // Search right
    for (size_t i = index + 1; i < size; i++)
    {
        if (array[i] == element)
        {
            return i;
        }
        if (*array[i] != *element)
        {
            break;
        }
    }

    return static_cast<size_t>(-1);
}