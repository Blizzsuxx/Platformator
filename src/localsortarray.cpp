#include "localsortarray.h"
#include "swapcallback.h"

LocalSortArray::LocalSortArray()
    : size(0), array(), checkpoint(), isDirty(true), leftChunk(nullptr), rightChunk(nullptr)
{
}

LocalSortArray::LocalSortArray(LocalSortArray *other)
    : size(other->size / 2), array(), checkpoint(other->checkpoint), isDirty(true), leftChunk(nullptr), rightChunk(nullptr)
{
    std::copy(other->array + size, other->array + MAX_SIZE, array);
    other->size = size;

    leftChunk = other;
    rightChunk = other->rightChunk;
    other->setRightChunk(this);

    for (int i = 0; i < size; i++)
    {
        array[i]->setChunk(this);

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
        element->setChunk(this);

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

BoundingRadiusProjection *LocalSortArray::pop()
{
    return remove(size - 1);
}

BoundingRadiusProjection *LocalSortArray::remove(size_t index)
{
    if (index >= size)
    {
        return nullptr;
    }
    // remove the element at the specified index
    BoundingRadiusProjection *removed = array[index];
    removed->setChunk(nullptr);

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

void LocalSortArray::sort(SwapCallback *callback)
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
            //     callback->swap(temp, array[j]);
            // }
            callback->swap(array[j], j, temp, i);
            j--;
        }
        callback->swap(array[j + 1], j + 1, temp, i);
    }
}

void LocalSortArray::sortFromIndex(size_t arrayIndex, SwapCallback *callback)
{
    // cross swap from left to right
    // swap to right until biggest
    if (arrayIndex == 0)
    {
        for (size_t i = 1; i < getSize(); i++)
        {
            if (*array[arrayIndex] > *array[i])
            {
                callback->swap(array[arrayIndex], arrayIndex, array[i], i);
                arrayIndex = i;
            }
            else
            {
                return;
            }
        }
    }
    // cross swap from right to left
    // swap to left until smallest
    else
    {
        for (size_t i = arrayIndex - 1; i != static_cast<size_t>(-1); i--)
        {
            if (*array[arrayIndex] < *array[i])
            {
                callback->swap(array[arrayIndex], arrayIndex, array[i], i);
                arrayIndex = i;
            }
            else
            {
                return;
            }
        }
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
    checkpoint.push_back(collider);
}

void LocalSortArray::removeCheckpoint(Collider *collider)
{
    checkpoint.erase(std::remove(checkpoint.begin(), checkpoint.end(), collider), checkpoint.end());
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

std::vector<Collider *> *LocalSortArray::getCheckpoint()
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
        else if (*array[mid] == *element)
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

void LocalSortArray::setIsDirty(bool dirty)
{
    isDirty = dirty;
}

bool LocalSortArray::getIsDirty() const
{
    return isDirty;
}