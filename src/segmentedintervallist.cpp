#include "segmentedintervallist.h"
#include "aabb.h"

SegmentedIntervalList::SegmentedIntervalList(AABB *owner)
    : chunks(), dirtyChunks(), owner(owner)
{
    chunks.push_back(new LocalSortArray(this));
}

SegmentedIntervalList::~SegmentedIntervalList()
{
    clear();
}

void SegmentedIntervalList::add(BoundingRadiusProjectionAxis *axis)
{
    BoundingRadiusProjection *lowerProjection = axis->getMin();
    size_t chunkWhereItWasInserted = add(lowerProjection);

    BoundingRadiusProjection *upperProjection = axis->getMax();
    size_t chunkWhereItWasInserted2 = add(upperProjection);

    for (size_t i = chunkWhereItWasInserted; i < chunkWhereItWasInserted2; i++)
    {
        chunks[i]->addCheckpoint(lowerProjection->getCollider());
    }
}

void SegmentedIntervalList::remove(BoundingRadiusProjectionAxis *axis)
{
    BoundingRadiusProjection *lowerProjection = axis->getMin();
    LocalSortArray *chunkWhereItWasRemoved = remove(lowerProjection);

    BoundingRadiusProjection *upperProjection = axis->getMax();
    LocalSortArray *chunkWhereItWasRemoved2 = remove(upperProjection);

    size_t chunkWhereItWasRemovedIndex = binarySearch(chunkWhereItWasRemoved);
    LocalSortArray *currentChunk = chunkWhereItWasRemoved;

    while (currentChunk != chunkWhereItWasRemoved2)
    {
        currentChunk->removeCheckpoint(lowerProjection->getCollider());
        chunkWhereItWasRemovedIndex++;
        currentChunk = chunks[chunkWhereItWasRemovedIndex];
    }
}

void SegmentedIntervalList::clear()
{
    for (size_t i = 0; i < chunks.size(); i++)
    {
        delete chunks[i];
    }
    chunks.clear();
    dirtyChunks.clear();
}

void SegmentedIntervalList::sort()
{
    // from lowest to highest
    // TODO: parallelize this
    for (LocalSortArray *chunk : dirtyChunks)
    {
        chunk->sort(this);
    }
    for (LocalSortArray *chunk : dirtyChunks)
    {
        LocalSortArray *currentChunk = chunk;
        LocalSortArray *leftChunk = currentChunk->getLeftChunk();
        LocalSortArray *rightChunk = currentChunk->getRightChunk();

        // check if the chunks are sorted between each other, if not, sort them
        while (leftChunk != nullptr && *leftChunk->getMax() > *currentChunk->getMin())
        {
            swapBoundaries(leftChunk, currentChunk);
            leftChunk->sortFromIndex(leftChunk->getSize() - 1, this);
            currentChunk->sortFromIndex(0, this);

            currentChunk = leftChunk;
            leftChunk = leftChunk->getLeftChunk();
        }

        currentChunk = chunk;
        while (rightChunk != nullptr && *rightChunk->getMin() < *currentChunk->getMax())
        {
            swapBoundaries(currentChunk, rightChunk);
            currentChunk->sortFromIndex(currentChunk->getSize() - 1, this);
            rightChunk->sortFromIndex(0, this);

            currentChunk = rightChunk;
            rightChunk = rightChunk->getRightChunk();
        }
    }

    dirtyChunks.clear();
}

void SegmentedIntervalList::swapBoundaries(LocalSortArray *leftChunk, LocalSortArray *rightChunk)
{
    swap(leftChunk->getMax(), leftChunk->getSize() - 1, rightChunk->getMin(), 0);
}

void SegmentedIntervalList::sortChunkFromIndex(LocalSortArray *chunk, size_t arrayIndex)
{
    // cross swap from left to right
    // swap to right until biggest
    if (arrayIndex == 0)
    {
        for (size_t i = 1; i < chunk->getSize(); i++)
        {
            if (*chunk->get(arrayIndex) > *chunk->get(i))
            {
                swap(chunk->get(arrayIndex), arrayIndex, chunk->get(i), i);
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
            if (*chunk->get(arrayIndex) < *chunk->get(i))
            {
                swap(chunk->get(arrayIndex), arrayIndex, chunk->get(i), i);
                arrayIndex = i;
            }
            else
            {
                return;
            }
        }
    }
}

size_t SegmentedIntervalList::binarySearch(BoundingRadiusProjection *element)
{
    if (chunks.empty() || (chunks[0]->getSize() == 0 && chunks.size() == 1))
    {
        return 0;
    }

    size_t low = 0;
    size_t high = chunks.size() - 1;
    size_t mid = 0;
    BoundingRadiusProjection value = *element;

    while (low <= high)
    {
        mid = (low + high) / 2;

        if (mid == 0)
        {
            return mid;
        }
        if (value <= *chunks[mid]->getMax() && value >= *chunks[mid]->getMin())
        {
            return mid;
        }
        else if (value >= *chunks[mid]->getMax())
        {
            low = mid + 1;
        }
        else if (value < *chunks[mid]->getMax())
        {
            high = mid - 1;
        }
    }

    return mid;
}

size_t SegmentedIntervalList::binarySearch(LocalSortArray *array)
{
    if (chunks.empty() || (chunks[0]->getSize() == 0 && chunks.size() == 1))
    {
        return 0;
    }

    size_t low = 0;
    size_t high = chunks.size() - 1;
    size_t mid = 0;

    while (low <= high)
    {
        mid = (low + high) / 2;

        if (chunks[mid] == array)
        {
            return mid;
        }
        else if (*chunks[mid]->getMax() < *array->getMin())
        {
            low = mid + 1;
        }
        else if (*chunks[mid]->getMin() > *array->getMax())
        {
            high = mid - 1;
        }
    }

    return mid;
}

size_t SegmentedIntervalList::add(BoundingRadiusProjection *element)
{
    // find the index of the element using binary search
    size_t index = binarySearch(element);

    // insert the element at the index
    return add(element, index);
}

size_t SegmentedIntervalList::add(BoundingRadiusProjection *element, size_t chunkIndex)
{
    // insert the element at the index
    if (!chunks[chunkIndex]->add(element))
    {
        // if the array is full, split it
        LocalSortArray *newArray = new LocalSortArray(chunks[chunkIndex]);

        // insert the new array after the current one
        chunks.insert(chunks.begin() + chunkIndex + 1, newArray);

        // check if the element should be inserted in the new array or the old one
        if (*element > *chunks[chunkIndex]->getMax())
        {
            chunkIndex++;
        }

        // insert the element in the correct array
        chunks[chunkIndex]->add(element);
    }

    return chunkIndex;
}

LocalSortArray *SegmentedIntervalList::remove(BoundingRadiusProjection *element)
{
    LocalSortArray *array = element->getChunk();
    array->remove(element);

    return array;
}

BoundingRadiusProjection *SegmentedIntervalList::remove(size_t chunkIndex, size_t arrayIndex)
{
    BoundingRadiusProjection *removedElement = chunks[chunkIndex]->remove(arrayIndex);

    // if the array is empty, remove it
    if (chunks[chunkIndex]->getSize() == 0)
    {
        if (chunks[chunkIndex]->getIsDirty())
        {
            auto it = std::find(dirtyChunks.begin(), dirtyChunks.end(), chunks[chunkIndex]);
            if (it != dirtyChunks.end())
            {
                dirtyChunks.erase(it);
            }
        }

        if (chunkIndex > 0)
        {
            chunks[chunkIndex - 1]->setRightChunk(chunks[chunkIndex]->getRightChunk());
        }
        if (chunkIndex < chunks.size() - 1)
        {
            chunks[chunkIndex + 1]->setLeftChunk(chunks[chunkIndex]->getLeftChunk());
        }

        delete chunks[chunkIndex];
        chunks.erase(chunks.begin() + chunkIndex);
    }

    return removedElement;
}

void SegmentedIntervalList::swap(BoundingRadiusProjection *leftRadiusProjection, size_t leftRadiusProjectionIndex, BoundingRadiusProjection *rightRadiusProjection, size_t rightRadiusProjectionIndex)
{
    // minimum of left crossing maximum of right means that the two projections are now no longer overlapping
    // maximum of left crossing minimum of right means that the two projections are now overlapping
    // if both projections are from the same collider, then don't add collision (it's probably either a very fast object or a really small object)
    // TODO: add the buffer for checkpoints (for fast moving objects)
    // we also need to check if it's a cross chunk swap (need to update checkpoints)
    // cross chunk:
    // right is minimum - add to checkpoint (left chunk) - minimum crossing left into a chunk
    // left is minimum - remove from checkpoint (left chunk) - minimum crossing right out of a chunk
    // left is maximum - add to checkpoint (left chunk) - maximum crossing right into a chunk
    // right is maximum - remove from checkpoint (left chunk) - maximum crossing left out of a chunk

    Collider *colliderA = leftRadiusProjection->getCollider();
    Collider *colliderB = rightRadiusProjection->getCollider();

    LocalSortArray *leftChunk = leftRadiusProjection->getChunk();
    LocalSortArray *rightChunk = rightRadiusProjection->getChunk();

    leftChunk->getArray()[leftRadiusProjectionIndex] = rightRadiusProjection;
    rightChunk->getArray()[rightRadiusProjectionIndex] = leftRadiusProjection;

    // first emit collision events for the two colliders if they are now overlapping
    if (colliderA != colliderB)
    {
        if (!leftRadiusProjection->getIsEnd() && rightRadiusProjection->getIsEnd())
        {
            // remove collision
            owner->addCandidateCollision(colliderA, colliderB);
        }
        else if (leftRadiusProjection->getIsEnd() && !rightRadiusProjection->getIsEnd())
        {
            // add collision
            owner->addCandidateCollision(colliderA, colliderB);
        }
    }

    // cross chunk swap - update checkpoints
    if (leftChunk != rightChunk)
    {
        leftRadiusProjection->setChunk(rightChunk);
        rightRadiusProjection->setChunk(leftChunk);
        // left is minimum - remove from checkpoint (left chunk) - minimum crossing right out of a chunk
        if (!leftRadiusProjection->getIsEnd())
        {
            leftChunk->removeCheckpoint(colliderA);
        }
        // left is maximum - add to checkpoint (left chunk) - maximum crossing right into a chunk
        else
        {
            leftChunk->addCheckpoint(colliderA);
        }
        // right is minimum - add to checkpoint (left chunk) - minimum crossing left into a chunk
        if (!rightRadiusProjection->getIsEnd())
        {
            leftChunk->addCheckpoint(colliderB);
        }
        // right is maximum - remove from checkpoint (left chunk) - maximum crossing left out of a chunk
        else
        {
            leftChunk->removeCheckpoint(colliderB);
        }
    }
}

void SegmentedIntervalList::addDirtyChunk(LocalSortArray *chunk)
{
    dirtyChunks.push_back(chunk);
}