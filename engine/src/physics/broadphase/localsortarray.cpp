#include "physics/broadphase/localsortarray.h"

#include <algorithm>

#include "buildconfig.h"
#include "physics/broadphase/segmentedintervallist.h"

LocalSortArray::LocalSortArray(SegmentedIntervalList *owner)
    : positions(), isMaxima(), array(), size(0), chunkListIndex(SIZE_MAX), checkpoints(), checkpointCache(),
      leftChunk(nullptr), rightChunk(nullptr), owner(owner)
{
}

LocalSortArray::LocalSortArray(LocalSortArray *other)
    : positions(), isMaxima(), array(), size(0), chunkListIndex(SIZE_MAX), checkpoints(), checkpointCache(),
      leftChunk(nullptr), rightChunk(nullptr), owner(other->owner)
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
    std::copy(other->positions + splitIndex, other->positions + oldSize, positions);
    std::copy(other->isMaxima + splitIndex, other->isMaxima + oldSize, isMaxima);
    std::copy(other->array + splitIndex, other->array + oldSize, array);
    other->size = splitIndex;

    other->rightChunk = this;

    if (rightChunk != nullptr)
    {
        rightChunk->leftChunk = this;
    }

    // Recompute the left chunk's checkpoints by walking the moved right half backwards:
    // encountering a maxima adds its collider; encountering a minima removes it.
    for (size_t i = size - 1; i != SIZE_MAX; i--)
    {
        array[i]->setChunk(this);
        array[i]->setChunkIndex(i);

        if (!owner->getIsPrimary())
        {
            continue;
        }

        if (isMaxima[i])
        {
            other->checkpoints.insert(array[i]->getCollider());
        }
        else
        {
            other->checkpoints.erase(array[i]->getCollider());
        }
    }
}

size_t LocalSortArray::add(platformator_detail::ProjectionProxy *element)
{
    if (size == MAX_CHUNK_SIZE)
    {
        return SIZE_MAX;
    }

    const float pos = element->getCachedProjectedPosition();
    const bool maxFlag = element->getIsMaxima();
    const size_t index = binarySearch(pos, maxFlag);
    element->setChunk(this);

    for (size_t i = size; i > index; i--)
    {
        positions[i] = positions[i - 1];
        isMaxima[i] = isMaxima[i - 1];
        array[i] = array[i - 1];
        array[i]->setChunkIndex(i);
    }
    positions[index] = pos;
    isMaxima[index] = maxFlag ? 1 : 0;
    array[index] = element;
    element->setChunkIndex(index);
    size++;

    return index;
}

platformator_detail::ProjectionProxy *LocalSortArray::remove(size_t index)
{
    if (index >= size)
    {
        return nullptr;
    }

    platformator_detail::ProjectionProxy *removed = array[index];
    removed->setChunk(nullptr);

    for (size_t i = index; i < size - 1; i++)
    {
        positions[i] = positions[i + 1];
        isMaxima[i] = isMaxima[i + 1];
        array[i] = array[i + 1];
        array[i]->setChunkIndex(i);
    }
    size--;

    return removed;
}

void LocalSortArray::addCheckpoint(Collider *collider)
{
    if (checkpointCache.erase(collider) != 0)
    {
        return;
    }

    checkpoints.insert(collider);
}

void LocalSortArray::removeCheckpoint(Collider *collider)
{
    if (checkpoints.erase(collider) == 0)
    {
        checkpointCache.insert(collider);
    }
}

size_t LocalSortArray::binarySearch(float pos, bool maxFlag) const noexcept
{
    size_t low = 0;
    size_t high = size;

    while (low < high)
    {
        const size_t mid = low + ((high - low) >> 1);
        if (platformator_detail::isProjectionKeyLess(positions[mid], isMaxima[mid] != 0, pos, maxFlag))
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
        const size_t rightSize = rightChunk->size;
        std::copy(rightChunk->positions, rightChunk->positions + rightSize, positions + size);
        std::copy(rightChunk->isMaxima, rightChunk->isMaxima + rightSize, isMaxima + size);
        for (size_t i = 0; i < rightSize; i++)
        {
            platformator_detail::ProjectionProxy *element = rightChunk->array[i];
            element->setChunk(this);
            element->setChunkIndex(size + i);
            array[size + i] = element;
        }
        size += rightSize;
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
    LocalSortArray *left = getLeftChunk();
    if (left != nullptr)
    {
        return left->tryMergeWithRightChunk();
    }

    return nullptr;
}
