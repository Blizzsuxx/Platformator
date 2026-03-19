#include "localsortarray.h"
#include "swapcallback.h"
#include "segmentedintervallist.h"

LocalSortArray::LocalSortArray(SegmentedIntervalList *owner)
    : size(0), array(), checkpoints(), isDirty(false), leftChunk(nullptr), rightChunk(nullptr), owner(owner)
{
}

LocalSortArray::LocalSortArray(LocalSortArray *other)
    : size(0), array(), checkpoints(other->checkpoints), isDirty(false), leftChunk(other), rightChunk(other->rightChunk), owner(other->owner)
{
    const size_t oldSize = other->size;
    const size_t movedCount = oldSize / 2;
    const size_t splitIndex = oldSize - movedCount;

    size = movedCount;
    std::copy(other->array + splitIndex, other->array + oldSize, array);
    other->size = splitIndex;

    other->rightChunk = this;

    if (rightChunk != nullptr)
    {
        rightChunk->leftChunk = this;
    }

    for (size_t i = 0; i < size; i++)
    {
        array[i]->setChunk(this);

        if (array[i]->getIsMaxima())
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

size_t LocalSortArray::add(BoundingRadiusProjection *element)
{
    // find the index where the element should be inserted using binary search
    if (size == MAX_SIZE)
    {
        return SIZE_MAX;
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

        return index;
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

size_t LocalSortArray::remove(BoundingRadiusProjection *element)
{
    // find the index of the element using binary search
    size_t index = find(element);

    if (index < size && array[index] == element)
    {
        remove(index);

        return index;
    }

    return SIZE_MAX;
}

void LocalSortArray::sort(SwapCallback *callback)
{
    // sort the array with insertion sort, sort from lowest to highest
    for (size_t i = 1; i < size; i++)
    {
        size_t j = i;

        while (j > 0 && *array[j - 1] > *array[j])
        {
            callback->swap(array[j - 1], j - 1, array[j], j);
            j--;
        }
    }

    this->setIsDirty(false);
}

void LocalSortArray::sortFromIndex(size_t arrayIndex, SwapCallback *callback)
{
    if (arrayIndex == 0)
    {
        while (arrayIndex + 1 < getSize() && *array[arrayIndex] > *array[arrayIndex + 1])
        {
            callback->swap(array[arrayIndex], arrayIndex, array[arrayIndex + 1], arrayIndex + 1);
            arrayIndex++;
        }
    }
    else
    {
        while (arrayIndex > 0 && *array[arrayIndex - 1] > *array[arrayIndex])
        {
            callback->swap(array[arrayIndex - 1], arrayIndex - 1, array[arrayIndex], arrayIndex);
            arrayIndex--;
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
    checkpoints.clear();
}

void LocalSortArray::addCheckpoint(Collider *collider)
{
    checkpoints.insert(collider);
}

void LocalSortArray::removeCheckpoint(Collider *collider)
{
    checkpoints.erase(collider);
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

std::unordered_set<Collider *> *LocalSortArray::getCheckpoints()
{
    return &checkpoints;
}

size_t LocalSortArray::find(BoundingRadiusProjection *element)
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
    if (this->isDirty == false && dirty == true)
    {
        owner->addDirtyChunk(this);
    }
    isDirty = dirty;
}

bool LocalSortArray::getIsDirty() const
{
    return isDirty;
}

LocalSortArray *LocalSortArray::getLeftChunk() const
{
    return leftChunk;
}

LocalSortArray *LocalSortArray::getRightChunk() const
{
    return rightChunk;
}

void LocalSortArray::setLeftChunk(LocalSortArray *leftChunk)
{
    this->leftChunk = leftChunk;
}

void LocalSortArray::setRightChunk(LocalSortArray *rightChunk)
{
    this->rightChunk = rightChunk;
}