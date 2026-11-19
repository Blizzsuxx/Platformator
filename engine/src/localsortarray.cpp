#include "localsortarray.h"

#include "buildconfig.h"
#include "segmentedintervallist.h"

LocalSortArray::LocalSortArray(SegmentedIntervalList *owner)
    : size(0), array(), checkpoints(), leftChunk(nullptr), rightChunk(nullptr), owner(owner)
{
}

LocalSortArray::LocalSortArray(LocalSortArray *other)
    : size(0), array(), checkpoints(), checkpointCache(), leftChunk(nullptr), rightChunk(nullptr), owner(other->owner)
{
    initFromSplit(other);
}

void LocalSortArray::initFromSplit(LocalSortArray *other)
{
    owner = other->owner;
    leftChunk = other;
    rightChunk = other->rightChunk;
    checkpointCache.clear();
    if (owner->getIsPrimary())
    {
        checkpoints = other->checkpoints;
    }
    else
    {
        checkpoints.clear();
    }

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
        const size_t mid = low + ((high - low) >> 1);
        if (*array[mid] < *element)
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

size_t LocalSortArray::logInvalidFind()
{
    PLATFORMATOR_LOG("Warning: element's cached index is invalid for some reason\n");
    return SIZE_MAX;
}

LocalSortArray *LocalSortArray::tryMergeWithRightChunk()
{
    if (rightChunk != nullptr && size + rightChunk->size <= CHUNK_MERGE_THRESHOLD)
    {
        for (size_t i = 0; i < rightChunk->size; i++)
        {
            BoundingRadiusProjectionProxy *element = rightChunk->array[i];
            element->setChunk(this);
            element->setChunkIndex(size + i);
            array[size + i] = element;
        }
        size += rightChunk->size;
        if (owner->getIsPrimary())
        {
            checkpoints = std::move(rightChunk->checkpoints);
            checkpointCache = std::move(rightChunk->checkpointCache);
        }

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