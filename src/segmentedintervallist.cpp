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
    BoundingRadiusProjection *upperProjection = axis->getMax();

    auto [chunkWhereItWasInserted, indexInsideChunkWhereItWasInserted] = add(lowerProjection);
    auto [chunkWhereItWasInserted2, indexInsideChunkWhereItWasInserted2] = add(upperProjection);

    LocalSortArray *currentChunk = chunkWhereItWasInserted;
    Collider *collider = lowerProjection->getCollider();

    while (currentChunk != chunkWhereItWasInserted2)
    {
        currentChunk->addCheckpoint(collider);
        currentChunk = currentChunk->getRightChunk();
    }

    addCollisionsForNewlyAddedProjection(lowerProjection, indexInsideChunkWhereItWasInserted, upperProjection, indexInsideChunkWhereItWasInserted2);
}

void SegmentedIntervalList::remove(BoundingRadiusProjectionAxis *axis)
{
    printf("Removing %s from segmented interval list\n", axis->getMin()->getCollider()->getGameObject()->getName().c_str());
    BoundingRadiusProjection *lowerProjection = axis->getMin();
    BoundingRadiusProjection *upperProjection = axis->getMax();

    size_t indexInsideChunkWhereItWillBeRemoved = lowerProjection->getChunk()->find(lowerProjection);
    size_t indexInsideChunkWhereItWillBeRemoved2 = upperProjection->getChunk()->find(upperProjection);

    removeCollisionsForRemovedProjection(lowerProjection, indexInsideChunkWhereItWillBeRemoved, upperProjection, indexInsideChunkWhereItWillBeRemoved2);

    auto [chunkWhereItWasRemoved, indexInsideChunkWhereItWasRemoved] = remove(lowerProjection);
    auto [chunkWhereItWasRemoved2, indexInsideChunkWhereItWasRemoved2] = remove(upperProjection);

    LocalSortArray *currentChunk = chunkWhereItWasRemoved;
    Collider *collider = lowerProjection->getCollider();

    while (currentChunk != chunkWhereItWasRemoved2)
    {
        currentChunk->removeCheckpoint(collider);
        currentChunk = currentChunk->getRightChunk();
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

std::pair<LocalSortArray *, size_t> SegmentedIntervalList::add(BoundingRadiusProjection *element)
{
    // find the index of the element using binary search
    size_t index = binarySearch(element);

    // insert the element at the index
    return add(element, index);
}

std::pair<LocalSortArray *, size_t> SegmentedIntervalList::add(BoundingRadiusProjection *element, size_t chunkIndex)
{
    // insert the element at the index
    size_t indexInsideChunk = chunks[chunkIndex]->add(element);
    if (indexInsideChunk == SIZE_MAX)
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
        indexInsideChunk = chunks[chunkIndex]->add(element);
    }

    return {chunks[chunkIndex], indexInsideChunk};
}

std::pair<LocalSortArray *, size_t> SegmentedIntervalList::remove(BoundingRadiusProjection *element)
{
    LocalSortArray *array = element->getChunk();
    size_t index = array->remove(element);

    if (index != SIZE_MAX)
    {
        return {array, index};
    }
    else
    {
        return {nullptr, SIZE_MAX};
    }
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
        printf("Swapping %s and %s\n", colliderA->getGameObject()->getName().c_str(), colliderB->getGameObject()->getName().c_str());
        if (!leftRadiusProjection->getIsEnd() && rightRadiusProjection->getIsEnd())
        {
            // remove collision
            owner->removeCandidateCollision(colliderA, colliderB);
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

// void AABB::checkForPotentialCollisionsInsideChunk(LocalSortArray *chunk)
// {
//     for (size_t i = 0; i < chunk->getSize(); i++)
//     {
//         BoundingRadiusProjection *projection = chunk->get(i);
//         Collider *collider = projection->getCollider();

//         if (collider->getGameObject()->getActive() == false || collider->getIsDirty() == false)
//         {
//             continue;
//         }

//         if (projection->getIsEnd())
//         {
//             for (size_t j = i - 1; j != static_cast<size_t>(-1); j--)
//             {

//                 Collider *previousProjection = chunk->get(j)->getCollider();
//                 if (previousProjection == collider)
//                 {
//                     break;
//                 }

//                 if (!chunk->get(j)->getIsEnd())
//                 {
//                     continue;
//                 }
//                 addCandidateCollision(collider, previousProjection);
//             }
//         }
//     }
// }

// void AABB::checkForCollisionsWithCheckpoint(LocalSortArray *chunk)
// {
//     for (Collider *checkpoint : *(chunk->getCheckpoint()))
//     {
//         if (checkpoint->getGameObject()->getActive() == false || checkpoint->getIsDirty() == false)
//         {
//             continue;
//         }

//         for (size_t i = chunk->getSize() - 1; i != static_cast<size_t>(-1); i--)
//         {
//             Collider *previousProjection = chunk->get(i)->getCollider();
//             if (previousProjection == checkpoint)
//             {
//                 break;
//             }

//             if (!chunk->get(i)->getIsEnd())
//             {
//                 continue;
//             }

//             addCandidateCollision(checkpoint, previousProjection);
//         }
//     }
// }

void SegmentedIntervalList::addCollisionsForNewlyAddedProjection(BoundingRadiusProjection *lowerProjection, size_t lowerIndexInsideChunkWhereItWasInserted, BoundingRadiusProjection *upperProjection, size_t upperIndexInsideChunkWhereItWasInserted)
{
    LocalSortArray *currentChunk = lowerProjection->getChunk();
    LocalSortArray *upperChunk = upperProjection->getChunk();
    LocalSortArray *targetChunk = upperChunk->getRightChunk();

    Collider *collider = lowerProjection->getCollider();
    size_t currentIndex = lowerIndexInsideChunkWhereItWasInserted;

    while (currentChunk != targetChunk)
    {
        BoundingRadiusProjection **projections = currentChunk->getArray();

        for (int i = currentIndex; i < currentChunk->getSize(); i++)
        {
            BoundingRadiusProjection *projection = projections[i];
            if (projection->getCollider() != collider)
            {
                owner->addCandidateCollision(collider, projection->getCollider());
            }

            if (projection == upperProjection)
            {
                break;
            }
        }

        currentChunk = currentChunk->getRightChunk();
        currentIndex = 0;
    }

    for (Collider *checkpoint : *upperProjection->getChunk()->getCheckpoint())
    {
        if (checkpoint != collider)
        {
            owner->addCandidateCollision(collider, checkpoint);
        }
    }
}

void SegmentedIntervalList::removeCollisionsForRemovedProjection(BoundingRadiusProjection *lowerProjection, size_t lowerIndexInsideChunkWhereItWasRemoved, BoundingRadiusProjection *upperProjection, size_t upperIndexInsideChunkWhereItWasRemoved)
{
    LocalSortArray *currentChunk = lowerProjection->getChunk();
    LocalSortArray *upperChunk = upperProjection->getChunk();
    LocalSortArray *targetChunk = upperChunk->getRightChunk();

    Collider *collider = lowerProjection->getCollider();
    size_t currentIndex = lowerIndexInsideChunkWhereItWasRemoved;

    while (currentChunk != targetChunk)
    {
        BoundingRadiusProjection **projections = currentChunk->getArray();

        for (int i = currentIndex; i < currentChunk->getSize(); i++)
        {
            BoundingRadiusProjection *projection = projections[i];
            if (projection->getCollider() != collider)
            {
                owner->removeCandidateCollision(collider, projection->getCollider());
            }

            if (projection == upperProjection)
            {
                break;
            }
        }

        currentChunk = currentChunk->getRightChunk();
        currentIndex = 0;
    }

    for (Collider *checkpoint : *upperProjection->getChunk()->getCheckpoint())
    {
        if (checkpoint != collider)
        {
            owner->removeCandidateCollision(collider, checkpoint);
        }
    }
}