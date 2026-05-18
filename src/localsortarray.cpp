#include "localsortarray.h"

#include "buildconfig.h"
#include "segmentedintervallist.h"

LocalSortArray::LocalSortArray(SegmentedIntervalList *owner)
    : size(0), array(), checkpoints(), leftChunk(nullptr), rightChunk(nullptr), owner(owner)
{
}

LocalSortArray::LocalSortArray(LocalSortArray *other)
    : size(0), array(), checkpoints(other->owner->getIsPrimary() ? other->checkpoints : std::unordered_set<Collider *>()), leftChunk(other), rightChunk(other->rightChunk), owner(other->owner)
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

    // When a chunk must be split due to insertion into a full chunk, a
    // new chunk is allocated and placed next in the list. Approximately
    // half the elements from the end of the full chunk are copied into the
    // new chunk, and the new chunk inherits the old chunk’s checkpoints
    // set. The checkpoints set for the old chunk is computed by starting
    //  with the original set and modifying this set while traversing the new
    // chunk. Beginning with the end and traversing backwards, when a
    // maxima is encountered, its object id is added to the checkpoints.
    // When a minima is encountered, its object id is removed.

    for (size_t i = size - 1; i != SIZE_MAX; i--)
    {
        array[i]->setChunk(this);
        array[i]->setChunkIndex(i);

        if (!owner->getIsPrimary())
        {
            continue;
        }

        if (array[i]->getIsMaxima())
        {
            other->addCheckpointInternal(array[i]->getCollider());
        }
        else
        {
            other->removeCheckpointInternal(array[i]->getCollider());
        }
    }
}

LocalSortArray::~LocalSortArray()
{
}

size_t LocalSortArray::add(BoundingRadiusProjectionProxy *element)
{
    // find the index where the element should be inserted using binary search
    if (size == MAX_CHUNK_SIZE)
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
            array[i]->setChunkIndex(i);
        }
        array[index] = element;
        element->setChunkIndex(index);
        size++;

        return index;
    }
}

BoundingRadiusProjectionProxy *LocalSortArray::pop()
{
    return remove(size - 1);
}

BoundingRadiusProjectionProxy *LocalSortArray::remove(size_t index)
{
    if (index >= size)
    {
        return nullptr;
    }
    // remove the element at the specified index
    BoundingRadiusProjectionProxy *removed = array[index];
    removed->setChunk(nullptr);

    for (size_t i = index; i < size - 1; i++)
    {
        array[i] = array[i + 1];
        array[i]->setChunkIndex(i);
    }
    size--;

    return removed;
}

size_t LocalSortArray::remove(BoundingRadiusProjectionProxy *element)
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

BoundingRadiusProjectionProxy *LocalSortArray::get(size_t index)
{
    return array[index];
}

BoundingRadiusProjectionProxy *LocalSortArray::operator[](size_t index)
{
    return array[index];
}

BoundingRadiusProjectionProxy *LocalSortArray::getMax()
{
    return array[size - 1];
}

BoundingRadiusProjectionProxy *LocalSortArray::getMin()
{
    return array[0];
}

size_t LocalSortArray::getSize() const
{
    return size;
}

BoundingRadiusProjectionProxy **LocalSortArray::getArray()
{
    return array;
}

void LocalSortArray::clear()
{
    size = 0;
    checkpoints.clear();
    checkpointCache.clear();
}

void LocalSortArray::addCheckpoint(Collider *collider)
{
    if (checkpointCache.find(collider) != checkpointCache.end())
    {
        checkpointCache.erase(collider);
        return;
    }

    addCheckpointInternal(collider);
}

void LocalSortArray::addCheckpointInternal(Collider *collider)
{
    checkpoints.insert(collider);
}

void LocalSortArray::removeCheckpoint(Collider *collider)
{
    size_t numberOfErasedMembers = removeCheckpointInternal(collider);

    if (numberOfErasedMembers == 0)
    {
        checkpointCache.insert(collider);
    }
}

size_t LocalSortArray::removeCheckpointInternal(Collider *collider)
{
    return checkpoints.erase(collider);
}

size_t LocalSortArray::binarySearch(BoundingRadiusProjectionProxy *element)
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

size_t LocalSortArray::find(BoundingRadiusProjectionProxy *element)
{
    size_t cachedIndex = element->getChunkIndex();
    if (cachedIndex < size && array[cachedIndex] == element)
    {
        return cachedIndex;
    }

    PLATFORMATOR_LOG("Warning: element's cached index is invalid for some reason\n");

    return SIZE_MAX;
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

SegmentedIntervalList *LocalSortArray::getOwner() const
{
    return owner;
}

void LocalSortArray::setOwner(SegmentedIntervalList *owner)
{
    this->owner = owner;
}

LocalSortArray *LocalSortArray::tryMergeWithRightChunk()
{
    if (rightChunk != nullptr && size + rightChunk->size <= MAX_CHUNK_SIZE)
    {
        for (size_t i = 0; i < rightChunk->size; i++)
        {
            BoundingRadiusProjectionProxy *element = rightChunk->array[i];
            element->setChunk(this);
            element->setChunkIndex(size + i);
            array[size + i] = element;
        }
        size += rightChunk->size;
        checkpoints = std::move(rightChunk->checkpoints);
        checkpointCache = std::move(rightChunk->checkpointCache);

        LocalSortArray *oldRightChunk = rightChunk;
        LocalSortArray *newRightChunk = rightChunk->getRightChunk();
        setRightChunk(newRightChunk);
        if (newRightChunk != nullptr)
        {
            newRightChunk->setLeftChunk(this);
        }

        return oldRightChunk;
    }

    return nullptr;
}

LocalSortArray *LocalSortArray::tryMergeWithLeftChunk()
{
    LocalSortArray *leftChunk = getLeftChunk();
    if (leftChunk != nullptr)
    {
        return leftChunk->tryMergeWithRightChunk();
    }

    return nullptr;
}