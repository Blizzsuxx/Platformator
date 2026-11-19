#include "segmentedintervallist.h"
#include "aabb.h"
#include "buildconfig.h"

SegmentedIntervalList::SegmentedIntervalList(AABB *owner, bool isPrimary)
    : chunks(), freeChunks(), owner(owner), isPrimary(isPrimary)
{
    chunks.push_back(new LocalSortArray(this));
}

SegmentedIntervalList::~SegmentedIntervalList()
{
    clear();
}

void SegmentedIntervalList::add(BoundingRadiusProjectionAxisBinding *binding)
{
    BoundingRadiusProjectionProxy *lowerProxy = &binding->minProxy;
    BoundingRadiusProjectionProxy *upperProxy = &binding->maxProxy;

    add(lowerProxy);
    add(upperProxy);

    if (!isPrimary)
    {
        return;
    }

    // have to find the proxies again to get their current chunk and index, since adding them might have caused them to move
    // also, this is cheap now because we index everything
    auto [chunkWhereItWasInserted, indexInsideChunkWhereItWasInserted] = find(lowerProxy);
    auto [chunkWhereItWasInserted2, indexInsideChunkWhereItWasInserted2] = find(upperProxy);

    if (chunkWhereItWasInserted == nullptr || chunkWhereItWasInserted2 == nullptr || indexInsideChunkWhereItWasInserted == SIZE_MAX || indexInsideChunkWhereItWasInserted2 == SIZE_MAX)
    {
        PLATFORMATOR_LOG("Error: trying to add a projection axis with stale proxy state, chunkWhereItWasInserted: %p, chunkWhereItWasInserted2: %p, indexInsideChunkWhereItWasInserted: %zu, indexInsideChunkWhereItWasInserted2: %zu\n", chunkWhereItWasInserted, chunkWhereItWasInserted2, indexInsideChunkWhereItWasInserted, indexInsideChunkWhereItWasInserted2);
        return;
    }

    Collider *collider = lowerProxy->getCollider();
    LocalSortArray *currentChunk = chunkWhereItWasInserted;
    while (currentChunk != chunkWhereItWasInserted2)
    {
        currentChunk->addCheckpoint(collider);
        currentChunk = currentChunk->getRightChunk();
    }

    addCollisionsForNewlyAddedProjection(lowerProxy, indexInsideChunkWhereItWasInserted, upperProxy, indexInsideChunkWhereItWasInserted2);
}

void SegmentedIntervalList::remove(BoundingRadiusProjectionAxisBinding *binding)
{
    BoundingRadiusProjectionProxy *lowerProxy = &binding->minProxy;
    BoundingRadiusProjectionProxy *upperProxy = &binding->maxProxy;

    if (!isPrimary)
    {
        remove(lowerProxy);
        remove(upperProxy);
        return;
    }

    auto [currentChunk, indexInsideChunkWhereItWillBeRemoved] = find(lowerProxy);
    auto [upperChunk, indexInsideChunkWhereItWillBeRemoved2] = find(upperProxy);

    if (currentChunk == nullptr || upperChunk == nullptr || indexInsideChunkWhereItWillBeRemoved == SIZE_MAX || indexInsideChunkWhereItWillBeRemoved2 == SIZE_MAX)
    {
        PLATFORMATOR_LOG("Error: trying to remove a projection axis with stale proxy state, currentChunk: %p, upperChunk: %p, indexInsideChunkWhereItWillBeRemoved: %zu, indexInsideChunkWhereItWillBeRemoved2: %zu\n", currentChunk, upperChunk, indexInsideChunkWhereItWillBeRemoved, indexInsideChunkWhereItWillBeRemoved2);
        return;
    }

    if (!owner->pairsWithBothAxisOverlapping.empty())
    {
        removeCollisionsForRemovedProjection(lowerProxy, indexInsideChunkWhereItWillBeRemoved, upperProxy, indexInsideChunkWhereItWillBeRemoved2);
    }

    if (currentChunk != upperChunk)
    {
        Collider *collider = lowerProxy->getCollider();
        LocalSortArray *chunk = currentChunk;
        while (chunk != upperChunk)
        {
            chunk->removeCheckpoint(collider);
            chunk = chunk->getRightChunk();
        }
    }

    remove(currentChunk, indexInsideChunkWhereItWillBeRemoved);
    remove(upperProxy);
}

void SegmentedIntervalList::clear()
{
    for (size_t i = 0; i < chunks.size(); i++)
    {
        delete chunks[i];
    }
    chunks.clear();
    for (size_t i = 0; i < freeChunks.size(); i++)
    {
        delete freeChunks[i];
    }
    freeChunks.clear();
}

LocalSortArray *SegmentedIntervalList::allocateSplitChunk(LocalSortArray *other)
{
    if (!freeChunks.empty())
    {
        LocalSortArray *chunk = freeChunks.back();
        freeChunks.pop_back();
        chunk->initFromSplit(other);
        return chunk;
    }
    return new LocalSortArray(other);
}

void SegmentedIntervalList::recycleChunk(LocalSortArray *chunk)
{
    if (freeChunks.size() < MAX_RECYCLED_CHUNKS_PER_LIST)
    {
        chunk->clear();
        chunk->setLeftChunk(nullptr);
        chunk->setRightChunk(nullptr);
        freeChunks.push_back(chunk);
    }
    else
    {
        delete chunk;
    }
}

void SegmentedIntervalList::repairProjection(BoundingRadiusProjectionProxy *projection)
{
    if (projection == nullptr)
    {
        PLATFORMATOR_LOG("Error: trying to repair a null projection\n");
        return;
    }

    LocalSortArray *chunk = projection->getChunk();
    if (chunk == nullptr)
    {
        PLATFORMATOR_LOG("Error: trying to repair a projection that is not in any chunk, projection: %p\n", projection);
        return;
    }

    size_t index = projection->getChunkIndex();
    if (index >= chunk->getSize() || chunk->get(index) != projection)
    {
        PLATFORMATOR_LOG("Error: trying to repair a projection that is in the chunk but has an invalid index, projection: %p, chunk: %p, index: %zu\n", projection, chunk, index);
        return;
    }

    repairProjectionFromIndexInternal(projection, chunk, index);
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

        PLATFORMATOR_LOG("Warning: found an empty chunk while looking for the previous index, leftChunk: %p\n", leftChunk);
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

        PLATFORMATOR_LOG("Warning: found an empty chunk while looking for the next index, rightChunk: %p\n", rightChunk);
        rightChunk = rightChunk->getRightChunk();
    }

    return {nullptr, SIZE_MAX};
}

void SegmentedIntervalList::repairProjectionFromIndexInternal(BoundingRadiusProjectionProxy *projection, LocalSortArray *chunk, size_t arrayIndex)
{
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
    if (chunks.size() <= 1)
    {
        return 0;
    }

    size_t low = 0;
    size_t high = chunks.size();
    const BoundingRadiusProjectionProxy &value = *element;

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
        LocalSortArray *newArray = allocateSplitChunk(chunks[chunkIndex]);

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
    size_t chunkSize = array->getSize();

    if (chunkSize == 0 && chunks.size() > 1)
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

        recycleChunk(array);

        return {rightChunk != nullptr ? rightChunk : leftChunk, index};
    }

    LocalSortArray *chunkToDelete = array->tryMergeWithLeftChunk();
    if (chunkToDelete != nullptr)
    {
        array = array->getLeftChunk();

        chunks.erase(std::remove(chunks.begin(), chunks.end(), chunkToDelete), chunks.end());
        recycleChunk(chunkToDelete);
    }
    chunkToDelete = array->tryMergeWithRightChunk();
    if (chunkToDelete != nullptr)
    {
        chunks.erase(std::remove(chunks.begin(), chunks.end(), chunkToDelete), chunks.end());
        recycleChunk(chunkToDelete);
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
            PLATFORMATOR_LOG("Error: trying to find an element that is not in the chunk it should be in, chunk: %p, element: %p\n", chunk, element);
        }
    }

    return {nullptr, SIZE_MAX};
}

void SegmentedIntervalList::swap(BoundingRadiusProjectionProxy *leftRadiusProjection, size_t leftRadiusProjectionIndex, BoundingRadiusProjectionProxy *rightRadiusProjection, size_t rightRadiusProjectionIndex)
{
    // minimum of left crossing maximum of right means that the two projections are now no longer overlapping
    // maximum of left crossing minimum of right means that the two projections are now overlapping
    // if both projections are from the same collider, then don't add collision (it's probably either a very fast object or a really small object)
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

void SegmentedIntervalList::emitCollision(Collider *colliderA, Collider *colliderB)
{
    owner->axisOverlapBegin(colliderA, colliderB);
}

void SegmentedIntervalList::removeCollision(Collider *colliderA, Collider *colliderB)
{
    owner->axisOverlapEnd(colliderA, colliderB);
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
    BoundingRadiusProjectionProxy **lowerArr = lowerChunk->getArray();
    const size_t lowerSz = lowerChunk->getSize();

    if (lowerChunk == upperChunk)
    {
        for (size_t i = lowerIndexInsideChunk + 1; i < upperIndexInsideChunk; ++i)
        {
            BoundingRadiusProjectionProxy *projection = lowerArr[i];
            if (projection->getIsMaxima())
            {
                emit(collider, projection->getCollider());
            }
        }

        Collider *openMinimaAfterUpper[MAX_CHUNK_SIZE];
        size_t openCount = 0;

        auto eraseOpenMinima = [&](Collider *target) {
            for (size_t k = 0; k < openCount; ++k)
            {
                if (openMinimaAfterUpper[k] == target)
                {
                    openMinimaAfterUpper[k] = openMinimaAfterUpper[--openCount];
                    return true;
                }
            }
            return false;
        };

        for (size_t i = upperIndexInsideChunk + 1; i < lowerSz; ++i)
        {
            BoundingRadiusProjectionProxy *projection = lowerArr[i];
            Collider *other = projection->getCollider();
            if (!projection->getIsMaxima())
            {
                openMinimaAfterUpper[openCount++] = other;
            }
            else if (!eraseOpenMinima(other))
            {
                emit(collider, other);
            }
        }

        if (!lowerChunk->checkpoints.empty())
        {
            for (Collider *checkpoint : lowerChunk->checkpoints)
            {
                if (checkpoint == collider)
                {
                    continue;
                }
                bool seenAfterUpper = false;
                for (size_t k = 0; k < openCount; ++k)
                {
                    if (openMinimaAfterUpper[k] == checkpoint)
                    {
                        seenAfterUpper = true;
                        break;
                    }
                }
                if (!seenAfterUpper)
                {
                    emit(collider, checkpoint);
                }
            }
        }
        return;
    }

    if (!lowerChunk->checkpoints.empty())
    {
        for (Collider *checkpoint : lowerChunk->checkpoints)
        {
            if (checkpoint != collider)
            {
                emit(collider, checkpoint);
            }
        }
    }

    for (size_t i = lowerIndexInsideChunk + 1; i < lowerSz; ++i)
    {
        BoundingRadiusProjectionProxy *projection = lowerArr[i];
        if (projection->getIsMaxima() && projection->getCollider() != collider)
        {
            emit(collider, projection->getCollider());
        }
    }

    for (LocalSortArray *currentChunk = lowerChunk->getRightChunk();
         currentChunk != nullptr && currentChunk != upperChunk;
         currentChunk = currentChunk->getRightChunk())
    {
        BoundingRadiusProjectionProxy **chunkArr = currentChunk->getArray();
        const size_t chunkSz = currentChunk->getSize();
        for (size_t i = 0; i < chunkSz; ++i)
        {
            BoundingRadiusProjectionProxy *projection = chunkArr[i];
            if (!projection->getIsMaxima() && projection->getCollider() != collider)
            {
                emit(collider, projection->getCollider());
            }
        }
    }

    BoundingRadiusProjectionProxy **upperArr = upperChunk->getArray();
    for (size_t i = 0; i < upperIndexInsideChunk; ++i)
    {
        BoundingRadiusProjectionProxy *projection = upperArr[i];
        if (!projection->getIsMaxima() && projection->getCollider() != collider)
        {
            emit(collider, projection->getCollider());
        }
    }
}