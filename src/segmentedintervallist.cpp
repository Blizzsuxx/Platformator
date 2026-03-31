#include "segmentedintervallist.h"
#include "aabb.h"

SegmentedIntervalList::SegmentedIntervalList(AABB *owner, Axis axis, bool isPrimary)
    : chunks(), owner(owner), axis(axis), isPrimary(isPrimary)
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
    Collider *collider = lowerProjection->getCollider();

    BoundingRadiusProjectionAxisProxy *axisProxy = axis->createProxyForList(this);

    BoundingRadiusProjectionProxy *lowerProxy = &axisProxy->minProxy;
    BoundingRadiusProjectionProxy *upperProxy = &axisProxy->maxProxy;
    auto [chunkWhereItWasInserted, indexInsideChunkWhereItWasInserted] = add(lowerProxy);
    auto [chunkWhereItWasInserted2, indexInsideChunkWhereItWasInserted2] = add(upperProxy);

    if (chunkWhereItWasInserted != lowerProxy->getChunk())
    {
        chunkWhereItWasInserted = lowerProxy->getChunk();
        indexInsideChunkWhereItWasInserted = chunkWhereItWasInserted->find(lowerProxy);
    }
    if (chunkWhereItWasInserted2 != upperProxy->getChunk())
    {
        chunkWhereItWasInserted2 = upperProxy->getChunk();
        indexInsideChunkWhereItWasInserted2 = chunkWhereItWasInserted2->find(upperProxy);
    }

    if (chunkWhereItWasInserted == nullptr || chunkWhereItWasInserted2 == nullptr || indexInsideChunkWhereItWasInserted == SIZE_MAX || indexInsideChunkWhereItWasInserted2 == SIZE_MAX)
    {
        printf("Error: trying to add a projection axis with stale proxy state, chunkWhereItWasInserted: %p, chunkWhereItWasInserted2: %p, indexInsideChunkWhereItWasInserted: %zu, indexInsideChunkWhereItWasInserted2: %zu\n", chunkWhereItWasInserted, chunkWhereItWasInserted2, indexInsideChunkWhereItWasInserted, indexInsideChunkWhereItWasInserted2);
        return;
    }

    LocalSortArray *currentChunk = chunkWhereItWasInserted;

    if (isPrimary)
    {
        while (currentChunk != chunkWhereItWasInserted2)
        {
            currentChunk->addCheckpoint(collider);
            currentChunk = currentChunk->getRightChunk();
        }
    }

    addCollisionsForNewlyAddedProjection(lowerProxy, indexInsideChunkWhereItWasInserted, upperProxy, indexInsideChunkWhereItWasInserted2);
}

void SegmentedIntervalList::remove(BoundingRadiusProjectionAxis *axis)
{
    Collider *collider = axis->getMin()->getCollider();
    BoundingRadiusProjectionAxisProxy *axisProxy = axis->getProxyForList(this);

    if (axisProxy == nullptr)
    {
        printf("Error: trying to remove a projection axis that is not in the list\n");
        return;
    }

    BoundingRadiusProjectionProxy *lowerProxy = &axisProxy->minProxy;
    BoundingRadiusProjectionProxy *upperProxy = &axisProxy->maxProxy;

    auto [currentChunk, indexInsideChunkWhereItWillBeRemoved] = find(lowerProxy);
    auto [upperChunk, indexInsideChunkWhereItWillBeRemoved2] = find(upperProxy);

    if (currentChunk == nullptr || upperChunk == nullptr || indexInsideChunkWhereItWillBeRemoved == SIZE_MAX || indexInsideChunkWhereItWillBeRemoved2 == SIZE_MAX)
    {
        printf("Error: trying to remove a projection axis with stale proxy state, currentChunk: %p, upperChunk: %p, indexInsideChunkWhereItWillBeRemoved: %zu, indexInsideChunkWhereItWillBeRemoved2: %zu\n", currentChunk, upperChunk, indexInsideChunkWhereItWillBeRemoved, indexInsideChunkWhereItWillBeRemoved2);
        return;
    }

    removeCollisionsForRemovedProjection(lowerProxy, indexInsideChunkWhereItWillBeRemoved, upperProxy, indexInsideChunkWhereItWillBeRemoved2);

    if (isPrimary)
    {
        while (currentChunk != upperChunk)
        {
            currentChunk->removeCheckpoint(collider);
            currentChunk = currentChunk->getRightChunk();
        }
    }

    if (currentChunk == upperChunk)
    {
        if (indexInsideChunkWhereItWillBeRemoved > indexInsideChunkWhereItWillBeRemoved2)
        {
            remove(currentChunk, indexInsideChunkWhereItWillBeRemoved);
            remove(upperChunk, indexInsideChunkWhereItWillBeRemoved2);
        }
        else
        {
            remove(upperChunk, indexInsideChunkWhereItWillBeRemoved2);
            remove(currentChunk, indexInsideChunkWhereItWillBeRemoved);
        }
    }
    else
    {
        remove(currentChunk, indexInsideChunkWhereItWillBeRemoved);
        remove(upperChunk, indexInsideChunkWhereItWillBeRemoved2);
    }

    removeDirtyProjection(lowerProxy);
    removeDirtyProjection(upperProxy);
    axis->removeProxy(axisProxy);
}

void SegmentedIntervalList::clear()
{
    for (size_t i = 0; i < chunks.size(); i++)
    {
        delete chunks[i];
    }
    chunks.clear();
    dirtyProjections.clear();
}

void SegmentedIntervalList::sort()
{
    for (BoundingRadiusProjectionProxy *projection : dirtyProjections)
    {
        if (projection == nullptr)
        {
            continue;
        }

        projection->setIsDirty(false);

        LocalSortArray *chunk = projection->getChunk();
        if (chunk == nullptr)
        {
            continue;
        }

        size_t index = projection->getChunkIndex();
        if (index >= chunk->getSize() || chunk->get(index) != projection)
        {
            printf("Error: trying to sort a projection that is not in the chunk it should be in, chunk: %p, projection: %p\n", chunk, projection);
            continue;
        }

        repairProjectionFromIndex(chunk, index);
    }

    dirtyProjections.clear();
}

std::pair<LocalSortArray *, size_t> SegmentedIntervalList::getPreviousIndex(LocalSortArray *chunk, size_t index) const
{
    if (chunk == nullptr)
    {
        return {nullptr, SIZE_MAX};
    }

    if (index > 0)
    {
        return {chunk, index - 1};
    }

    LocalSortArray *leftChunk = chunk->getLeftChunk();
    while (leftChunk != nullptr)
    {
        if (leftChunk->getSize() > 0)
        {
            return {leftChunk, leftChunk->getSize() - 1};
        }

        printf("Warning: found an empty chunk while looking for the previous index, leftChunk: %p\n", leftChunk);
        leftChunk = leftChunk->getLeftChunk();
    }

    return {nullptr, SIZE_MAX};
}

std::pair<LocalSortArray *, size_t> SegmentedIntervalList::getNextIndex(LocalSortArray *chunk, size_t index) const
{
    if (chunk == nullptr)
    {
        return {nullptr, SIZE_MAX};
    }

    if (index + 1 < chunk->getSize())
    {
        return {chunk, index + 1};
    }

    LocalSortArray *rightChunk = chunk->getRightChunk();
    while (rightChunk != nullptr)
    {
        if (rightChunk->getSize() > 0)
        {
            return {rightChunk, 0};
        }

        printf("Warning: found an empty chunk while looking for the next index, rightChunk: %p\n", rightChunk);
        rightChunk = rightChunk->getRightChunk();
    }

    return {nullptr, SIZE_MAX};
}

void SegmentedIntervalList::swapBoundaries(LocalSortArray *leftChunk, LocalSortArray *rightChunk)
{
    swap(leftChunk->getMax(), leftChunk->getSize() - 1, rightChunk->getMin(), 0);
}

void SegmentedIntervalList::repairProjectionFromIndex(LocalSortArray *chunk, size_t arrayIndex)
{
    if (chunk == nullptr || arrayIndex >= chunk->getSize())
    {
        return;
    }

    BoundingRadiusProjectionProxy *projection = chunk->get(arrayIndex);
    if (projection == nullptr)
    {
        return;
    }

    while (true)
    {
        chunk = projection->getChunk();
        arrayIndex = projection->getChunkIndex();

        auto [previousChunk, previousIndex] = getPreviousIndex(chunk, arrayIndex);
        if (previousChunk == nullptr)
        {
            break;
        }

        BoundingRadiusProjectionProxy *previousProjection = previousChunk->get(previousIndex);
        if (*previousProjection > *projection)
        {
            swap(previousProjection, previousIndex, projection, arrayIndex);
            continue;
        }

        break;
    }

    while (true)
    {
        chunk = projection->getChunk();
        arrayIndex = projection->getChunkIndex();

        auto [nextChunk, nextIndex] = getNextIndex(chunk, arrayIndex);
        if (nextChunk == nullptr)
        {
            break;
        }

        BoundingRadiusProjectionProxy *nextProjection = nextChunk->get(nextIndex);
        if (*projection > *nextProjection)
        {
            swap(projection, arrayIndex, nextProjection, nextIndex);
            continue;
        }

        break;
    }
}

size_t SegmentedIntervalList::binarySearch(BoundingRadiusProjectionProxy *element)
{
    if (chunks.empty() || (chunks[0]->getSize() == 0 && chunks.size() == 1))
    {
        return 0;
    }

    size_t low = 0;
    size_t high = chunks.size();
    BoundingRadiusProjectionProxy value = *element;

    while (low < high)
    {
        size_t mid = low + (high - low) / 2;
        if (*chunks[mid]->getMax() < value)
        {
            low = mid + 1;
        }
        else
        {
            high = mid;
        }
    }

    return low < chunks.size() ? low : chunks.size() - 1;
}

size_t SegmentedIntervalList::binarySearch(LocalSortArray *array)
{
    if (chunks.empty() || (chunks[0]->getSize() == 0 && chunks.size() == 1))
    {
        return 0;
    }

    size_t low = 0;
    size_t high = chunks.size();

    while (low < high)
    {
        size_t mid = low + (high - low) / 2;

        if (chunks[mid] == array)
        {
            return mid;
        }
        else if (*chunks[mid]->getMax() < *array->getMin())
        {
            low = mid + 1;
        }
        else
        {
            high = mid;
        }
    }

    return low < chunks.size() ? low : chunks.size() - 1;
}

std::pair<LocalSortArray *, size_t> SegmentedIntervalList::add(BoundingRadiusProjectionProxy *element)
{
    // find the index of the element using binary search
    size_t index = binarySearch(element);

    // insert the element at the index
    return add(element, index);
}

std::pair<LocalSortArray *, size_t> SegmentedIntervalList::add(BoundingRadiusProjectionProxy *element, size_t chunkIndex)
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

std::pair<LocalSortArray *, size_t> SegmentedIntervalList::remove(BoundingRadiusProjectionProxy *element)
{
    auto [array, index] = find(element);

    if (array == nullptr)
    {
        return {nullptr, SIZE_MAX};
    }

    return remove(array, index);
}

std::pair<LocalSortArray *, size_t> SegmentedIntervalList::remove(LocalSortArray *array, size_t index)
{
    if (array == nullptr || index == SIZE_MAX || index >= array->getSize())
    {
        return {nullptr, SIZE_MAX};
    }

    array->remove(index);

    if (array->getSize() == 0 && chunks.size() > 1)
    {
        LocalSortArray *leftChunk = array->getLeftChunk();
        LocalSortArray *rightChunk = array->getRightChunk();

        if (leftChunk != nullptr)
        {
            leftChunk->setRightChunk(rightChunk);
        }
        if (rightChunk != nullptr)
        {
            rightChunk->setLeftChunk(leftChunk);
        }

        chunks.erase(std::remove(chunks.begin(), chunks.end(), array), chunks.end());

        delete array;

        return {rightChunk != nullptr ? rightChunk : leftChunk, index};
    }

    return {array, index};
}

std::pair<LocalSortArray *, size_t> SegmentedIntervalList::find(BoundingRadiusProjectionProxy *element)
{
    LocalSortArray *chunk = element->getChunk();
    if (chunk != nullptr)
    {
        size_t index = chunk->find(element);
        if (index != SIZE_MAX)
        {
            return {chunk, index};
        }
        else
        {
            printf("Error: trying to find an element that is not in the chunk it should be in, chunk: %p, element: %p\n", chunk, element);
        }
    }

    return {nullptr, SIZE_MAX};
}

void SegmentedIntervalList::swap(BoundingRadiusProjectionProxy *leftRadiusProjection, size_t leftRadiusProjectionIndex, BoundingRadiusProjectionProxy *rightRadiusProjection, size_t rightRadiusProjectionIndex)
{
    // minimum of left crossing maximum of right means that the two projections are now no longer overlapping
    // maximum of left crossing minimum of right means that the two projections are now overlapping
    // if both projections are from the same collider, then don't add collision (it's probably either a very fast object or a really small object)
    // TODO: add the buffer for checkpoints (for fast moving objects) - done
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
    rightRadiusProjection->setChunkIndex(leftRadiusProjectionIndex);
    leftRadiusProjection->setChunkIndex(rightRadiusProjectionIndex);

    // first emit collision events for the two colliders if they are now overlapping
    if (colliderA != colliderB)
    {
        if (!leftRadiusProjection->getIsMaxima() && rightRadiusProjection->getIsMaxima())
        {
            // remove collision
            removeCollision(colliderA, colliderB);
        }
        else if (leftRadiusProjection->getIsMaxima() && !rightRadiusProjection->getIsMaxima())
        {
            // add collision
            emitCollision(colliderA, colliderB);
        }
    }

    // cross chunk swap - update checkpoints
    if (leftChunk != rightChunk)
    {
        leftRadiusProjection->setChunk(rightChunk);
        rightRadiusProjection->setChunk(leftChunk);

        if (!isPrimary)
        {
            return;
        }

        // left is minimum - remove from checkpoint (left chunk) - minimum crossing right out of a chunk
        if (!leftRadiusProjection->getIsMaxima())
        {
            leftChunk->removeCheckpoint(colliderA);
        }
        // left is maximum - add to checkpoint (left chunk) - maximum crossing right into a chunk
        else
        {
            leftChunk->addCheckpoint(colliderA);
        }
        // right is minimum - add to checkpoint (left chunk) - minimum crossing left into a chunk
        if (!rightRadiusProjection->getIsMaxima())
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

void SegmentedIntervalList::addDirtyProjection(BoundingRadiusProjectionProxy *projection)
{
    if (projection == nullptr || projection->getIsDirty())
    {
        return;
    }

    LocalSortArray *chunk = projection->getChunk();
    if (chunk == nullptr)
    {
        return;
    }

    projection->setIsDirty(true);
    dirtyProjections.push_back(projection);
}

void SegmentedIntervalList::removeDirtyProjection(BoundingRadiusProjectionProxy *projection)
{
    if (projection == nullptr || !projection->getIsDirty())
    {
        return;
    }

    projection->setIsDirty(false);
    dirtyProjections.erase(std::remove(dirtyProjections.begin(), dirtyProjections.end(), projection), dirtyProjections.end());
}

void SegmentedIntervalList::emitCollision(Collider *colliderA, Collider *colliderB)
{
    owner->axisOverlapBegin(colliderA, colliderB, axis);
}

void SegmentedIntervalList::removeCollision(Collider *colliderA, Collider *colliderB)
{
    owner->axisOverlapEnd(colliderA, colliderB, axis);
}

void SegmentedIntervalList::addCollisionsForNewlyAddedProjection(BoundingRadiusProjectionProxy *lowerProjection, size_t lowerIndexInsideChunkWhereItWasInserted, BoundingRadiusProjectionProxy *upperProjection, size_t upperIndexInsideChunkWhereItWasInserted)
{
    this->processProjectionCollisions(
        lowerProjection,
        lowerIndexInsideChunkWhereItWasInserted,
        upperProjection,
        upperIndexInsideChunkWhereItWasInserted,
        [this](Collider *colliderA, Collider *colliderB)
        {
            owner->overlapBeginCheckpoint(colliderA, colliderB);
        });
}

void SegmentedIntervalList::removeCollisionsForRemovedProjection(BoundingRadiusProjectionProxy *lowerProjection, size_t lowerIndexInsideChunkWhereItWasRemoved, BoundingRadiusProjectionProxy *upperProjection, size_t upperIndexInsideChunkWhereItWasRemoved)
{
    this->processProjectionCollisions(
        lowerProjection,
        lowerIndexInsideChunkWhereItWasRemoved,
        upperProjection,
        upperIndexInsideChunkWhereItWasRemoved,
        [this](Collider *colliderA, Collider *colliderB)
        {
            owner->overlapEndCheckpoint(colliderA, colliderB);
        });
}

template <typename EmitFn>
void SegmentedIntervalList::processProjectionCollisions(
    BoundingRadiusProjectionProxy *lowerProjection,
    size_t lowerIndexInsideChunk,
    BoundingRadiusProjectionProxy *upperProjection,
    size_t upperIndexInsideChunk,
    EmitFn &&emit)
{
    LocalSortArray *lowerChunk = lowerProjection->getChunk();
    LocalSortArray *upperChunk = upperProjection->getChunk();
    Collider *collider = lowerProjection->getCollider();

    std::array<Collider *, MAX_SIZE> minimaSeenAfterUpper{};
    size_t minimaSeenAfterUpperCount = 0;
    bool hasReachedUpper = (lowerChunk != upperChunk);

    auto containsMinimaSeenAfterUpper = [&](Collider *target)
    {
        for (size_t i = 0; i < minimaSeenAfterUpperCount; ++i)
        {
            if (minimaSeenAfterUpper[i] == target)
            {
                return true;
            }
        }

        return false;
    };

    if (lowerChunk == upperChunk)
    {
        for (size_t i = upperIndexInsideChunk + 1; i < lowerChunk->getSize(); i++)
        {
            BoundingRadiusProjectionProxy *projection = lowerChunk->get(i);
            if (projection->getCollider() != collider && !projection->getIsMaxima() && !containsMinimaSeenAfterUpper(projection->getCollider()))
            {
                minimaSeenAfterUpper[minimaSeenAfterUpperCount++] = projection->getCollider();
            }
        }
    }

    if (isPrimary)
    {
        for (Collider *checkpoint : *lowerChunk->getCheckpoints())
        {
            if (lowerChunk != upperChunk || !containsMinimaSeenAfterUpper(checkpoint))
            {
                emit(collider, checkpoint);
            }
        }
    }

    for (size_t i = lowerIndexInsideChunk + 1; i < lowerChunk->getSize(); i++)
    {
        BoundingRadiusProjectionProxy *projection = lowerChunk->get(i);

        if (projection->getCollider() == collider)
        {
            hasReachedUpper = true;
            continue;
        }

        if (!hasReachedUpper)
        {
            if (projection->getIsMaxima())
            {
                emit(collider, projection->getCollider());
            }
        }
        else if (projection->getIsMaxima() && !containsMinimaSeenAfterUpper(projection->getCollider()))
        {
            emit(collider, projection->getCollider());
        }
    }

    for (LocalSortArray *currentChunk = lowerChunk->getRightChunk(); currentChunk != nullptr && currentChunk != upperChunk; currentChunk = currentChunk->getRightChunk())
    {
        for (size_t i = 0; i < currentChunk->getSize(); i++)
        {
            BoundingRadiusProjectionProxy *projection = currentChunk->get(i);
            if (projection->getCollider() != collider && !projection->getIsMaxima())
            {
                emit(collider, projection->getCollider());
            }
        }
    }

    if (upperChunk != lowerChunk)
    {
        for (size_t i = 0; i < upperIndexInsideChunk; i++)
        {
            BoundingRadiusProjectionProxy *projection = upperChunk->get(i);
            if (projection->getCollider() != collider && !projection->getIsMaxima())
            {
                emit(collider, projection->getCollider());
            }
        }
    }
}

bool SegmentedIntervalList::getIsPrimary() const
{
    return isPrimary;
}

bool SegmentedIntervalList::getIsEmpty() const
{
    return chunks.size() == 1 && chunks[0]->getSize() == 0;
}