#include "physics/broadphase/segmentedintervallist.h"
#include "physics/broadphase/aabb.h"
#include "buildconfig.h"

#include <cassert>

namespace
{
bool isKeyGreater(float posA, bool maxA, float posB, bool maxB) noexcept
{
    return posA > posB || (posA == posB && maxA && !maxB);
}

bool isKeyLess(float posA, bool maxA, float posB, bool maxB) noexcept
{
    return posA < posB || (posA == posB && !maxA && maxB);
}
} // namespace

SegmentedIntervalList::SegmentedIntervalList(AABB *owner, bool isPrimary)
    : chunks(), freeChunks(), owner(owner), isPrimary(isPrimary)
{
    auto *first = new LocalSortArray(this);
    first->chunkListIndex = 0;
    chunks.push_back(first);
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

    auto [chunkWhereItWasInserted, indexInsideChunkWhereItWasInserted] = find(lowerProxy);
    auto [chunkWhereItWasInserted2, indexInsideChunkWhereItWasInserted2] = find(upperProxy);

    if (chunkWhereItWasInserted == nullptr || chunkWhereItWasInserted2 == nullptr ||
        indexInsideChunkWhereItWasInserted == SIZE_MAX || indexInsideChunkWhereItWasInserted2 == SIZE_MAX)
    {
        PLATFORMATOR_LOG("Error: trying to add a projection axis with stale proxy state, chunkWhereItWasInserted: %p, "
                         "chunkWhereItWasInserted2: %p, indexInsideChunkWhereItWasInserted: %zu, "
                         "indexInsideChunkWhereItWasInserted2: %zu\n",
                         chunkWhereItWasInserted, chunkWhereItWasInserted2, indexInsideChunkWhereItWasInserted,
                         indexInsideChunkWhereItWasInserted2);
        return;
    }

    Collider *collider = lowerProxy->getCollider();
    LocalSortArray *currentChunk = chunkWhereItWasInserted;
    while (currentChunk != chunkWhereItWasInserted2)
    {
        currentChunk->addCheckpoint(collider);
        currentChunk = currentChunk->getRightChunk();
    }

    addCollisionsForNewlyAddedProjection(lowerProxy, indexInsideChunkWhereItWasInserted, upperProxy,
                                         indexInsideChunkWhereItWasInserted2);
}

void SegmentedIntervalList::addBulkEmpty(const std::vector<BoundingRadiusProjectionProxy *> &sortedProxies)
{
    assert(getIsEmpty());
    if (sortedProxies.empty())
    {
        return;
    }

    const size_t total = sortedProxies.size();
    const size_t chunkCount = (total + BULK_CHUNK_FILL - 1) / BULK_CHUNK_FILL;
    chunks.reserve(chunkCount);

    LocalSortArray *prevChunk = chunks[0];
    prevChunk->chunkListIndex = 0;
    size_t cursor = 0;

    for (size_t c = 0; c < chunkCount; ++c)
    {
        LocalSortArray *chunk = (c == 0) ? prevChunk : allocateEmptyChunk();
        if (c > 0)
        {
            chunk->chunkListIndex = c;
            chunk->setLeftChunk(prevChunk);
            prevChunk->setRightChunk(chunk);
            chunks.push_back(chunk);
            prevChunk = chunk;
        }
        const size_t end = std::min(total, cursor + BULK_CHUNK_FILL);
        for (; cursor < end; ++cursor)
        {
            chunk->appendSorted(sortedProxies[cursor]);
        }
    }

    if (!isPrimary)
    {
        return;
    }

    // One linear sweep populates chunk checkpoints and emits all overlapping intervals.
    std::vector<Collider *> active;
    active.reserve(total / 2);

    for (LocalSortArray *chunk : chunks)
    {
        BoundingRadiusProjectionProxy **arr = chunk->getArray();
        const size_t sz = chunk->getSize();
        for (size_t i = 0; i < sz; ++i)
        {
            Collider *collider = arr[i]->getCollider();
            if (!chunk->getIsMaxima(i))
            {
                for (Collider *other : active)
                {
                    owner->overlapBeginCheckpoint(collider, other);
                }
                active.push_back(collider);
            }
            else
            {
                for (size_t k = 0; k < active.size(); ++k)
                {
                    if (active[k] == collider)
                    {
                        active[k] = active.back();
                        active.pop_back();
                        break;
                    }
                }
            }
        }
        for (Collider *collider : active)
        {
            chunk->addCheckpointInternal(collider);
        }
    }
    assert(active.empty());
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

    if (currentChunk == nullptr || upperChunk == nullptr || indexInsideChunkWhereItWillBeRemoved == SIZE_MAX ||
        indexInsideChunkWhereItWillBeRemoved2 == SIZE_MAX)
    {
        PLATFORMATOR_LOG("Error: trying to remove a projection axis with stale proxy state, currentChunk: %p, "
                         "upperChunk: %p, indexInsideChunkWhereItWillBeRemoved: %zu, "
                         "indexInsideChunkWhereItWillBeRemoved2: %zu\n",
                         currentChunk, upperChunk, indexInsideChunkWhereItWillBeRemoved,
                         indexInsideChunkWhereItWillBeRemoved2);
        return;
    }

    if (!owner->pairsWithBothAxisOverlapping.empty())
    {
        removeCollisionsForRemovedProjection(lowerProxy, indexInsideChunkWhereItWillBeRemoved, upperProxy,
                                             indexInsideChunkWhereItWillBeRemoved2);
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

void SegmentedIntervalList::resetToEmpty()
{
    if (chunks.empty())
    {
        auto *first = allocateEmptyChunk();
        first->chunkListIndex = 0;
        chunks.push_back(first);
        return;
    }
    for (size_t i = chunks.size(); i > 1; --i)
    {
        recycleChunk(chunks[i - 1]);
    }
    chunks.resize(1);
    chunks[0]->clear();
    chunks[0]->setLeftChunk(nullptr);
    chunks[0]->setRightChunk(nullptr);
    chunks[0]->chunkListIndex = 0;
}

LocalSortArray *SegmentedIntervalList::allocateEmptyChunk()
{
    if (!freeChunks.empty())
    {
        LocalSortArray *chunk = freeChunks.back();
        freeChunks.pop_back();
        chunk->setOwner(this);
        chunk->clear();
        chunk->setLeftChunk(nullptr);
        chunk->setRightChunk(nullptr);
        return chunk;
    }
    return new LocalSortArray(this);
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
    chunk->chunkListIndex = SIZE_MAX;
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

void SegmentedIntervalList::eraseChunkFromList(LocalSortArray *target)
{
    const size_t pos = target->chunkListIndex;
    assert(pos < chunks.size() && chunks[pos] == target);
    chunks.erase(chunks.begin() + static_cast<std::ptrdiff_t>(pos));
    for (size_t i = pos; i < chunks.size(); ++i)
    {
        chunks[i]->chunkListIndex = i;
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
        PLATFORMATOR_LOG("Error: trying to repair a projection that is in the chunk but has an invalid index, "
                         "projection: %p, chunk: %p, index: %zu\n",
                         projection, chunk, index);
        return;
    }

    chunk->setPosition(index, projection->getCachedProjectedPosition());
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

        PLATFORMATOR_LOG("Warning: found an empty chunk while looking for the previous index, leftChunk: %p\n",
                         leftChunk);
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

        PLATFORMATOR_LOG("Warning: found an empty chunk while looking for the next index, rightChunk: %p\n",
                         rightChunk);
        rightChunk = rightChunk->getRightChunk();
    }

    return {nullptr, SIZE_MAX};
}

void SegmentedIntervalList::repairProjectionFromIndexInternal(BoundingRadiusProjectionProxy *projection,
                                                              LocalSortArray *chunk, size_t arrayIndex)
{
    const float pos = projection->getCachedProjectedPosition();
    const bool maxFlag = projection->getIsMaxima();

    while (true)
    {
        auto [previousChunk, previousIndex] = getPreviousIndex(chunk, arrayIndex);
        if (previousChunk == nullptr)
        {
            break;
        }

        if (isKeyGreater(previousChunk->getPosition(previousIndex), previousChunk->getIsMaxima(previousIndex), pos,
                         maxFlag))
        {
            swap(previousChunk->get(previousIndex), previousIndex, projection, arrayIndex);
            chunk = previousChunk;
            arrayIndex = previousIndex;
            continue;
        }

        break;
    }

    while (true)
    {
        auto [nextChunk, nextIndex] = getNextIndex(chunk, arrayIndex);
        if (nextChunk == nullptr)
        {
            break;
        }

        if (isKeyGreater(pos, maxFlag, nextChunk->getPosition(nextIndex), nextChunk->getIsMaxima(nextIndex)))
        {
            swap(projection, arrayIndex, nextChunk->get(nextIndex), nextIndex);
            chunk = nextChunk;
            arrayIndex = nextIndex;
            continue;
        }

        break;
    }
}

size_t SegmentedIntervalList::binarySearch(BoundingRadiusProjectionProxy *element) const noexcept
{
    if (chunks.size() <= 1)
    {
        return 0;
    }

    size_t low = 0;
    size_t high = chunks.size();
    const float pos = element->getCachedProjectedPosition();
    const bool maxFlag = element->getIsMaxima();

    while (low < high)
    {
        const size_t mid = low + ((high - low) >> 1);
        if (isKeyLess(chunks[mid]->getMaxPosition(), chunks[mid]->getMaxIsMaxima(), pos, maxFlag))
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
    size_t index = binarySearch(element);
    return add(element, index);
}

std::pair<LocalSortArray *, size_t> SegmentedIntervalList::add(BoundingRadiusProjectionProxy *element,
                                                               size_t chunkIndex)
{
    size_t indexInsideChunk = chunks[chunkIndex]->add(element);
    if (indexInsideChunk == SIZE_MAX)
    {
        LocalSortArray *newArray = allocateSplitChunk(chunks[chunkIndex]);
        chunks.insert(chunks.begin() + static_cast<std::ptrdiff_t>(chunkIndex + 1), newArray);
        for (size_t i = chunkIndex + 1; i < chunks.size(); ++i)
        {
            chunks[i]->chunkListIndex = i;
        }

        if (isKeyGreater(element->getCachedProjectedPosition(), element->getIsMaxima(),
                         chunks[chunkIndex]->getMaxPosition(), chunks[chunkIndex]->getMaxIsMaxima()))
        {
            chunkIndex++;
        }

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

        eraseChunkFromList(array);
        recycleChunk(array);

        return {rightChunk != nullptr ? rightChunk : leftChunk, index};
    }

    LocalSortArray *chunkToDelete = array->tryMergeWithLeftChunk();
    if (chunkToDelete != nullptr)
    {
        array = array->getLeftChunk();
        eraseChunkFromList(chunkToDelete);
        recycleChunk(chunkToDelete);
    }
    chunkToDelete = array->tryMergeWithRightChunk();
    if (chunkToDelete != nullptr)
    {
        eraseChunkFromList(chunkToDelete);
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
            PLATFORMATOR_LOG("Error: trying to find an element that is not in the chunk it should be in, chunk: %p, "
                             "element: %p\n",
                             chunk, element);
        }
    }

    return {nullptr, SIZE_MAX};
}

void SegmentedIntervalList::swap(BoundingRadiusProjectionProxy *leftRadiusProjection, size_t leftRadiusProjectionIndex,
                                 BoundingRadiusProjectionProxy *rightRadiusProjection,
                                 size_t rightRadiusProjectionIndex)
{
    Collider *colliderA = leftRadiusProjection->getCollider();
    Collider *colliderB = rightRadiusProjection->getCollider();

    LocalSortArray *leftChunk = leftRadiusProjection->getChunk();
    LocalSortArray *rightChunk = rightRadiusProjection->getChunk();

    const float leftPos = leftChunk->getPosition(leftRadiusProjectionIndex);
    const bool leftMax = leftChunk->getIsMaxima(leftRadiusProjectionIndex);
    const float rightPos = rightChunk->getPosition(rightRadiusProjectionIndex);
    const bool rightMax = rightChunk->getIsMaxima(rightRadiusProjectionIndex);

    leftChunk->setSlot(leftRadiusProjectionIndex, rightRadiusProjection, rightPos, rightMax);
    rightChunk->setSlot(rightRadiusProjectionIndex, leftRadiusProjection, leftPos, leftMax);
    rightRadiusProjection->setChunkIndex(leftRadiusProjectionIndex);
    leftRadiusProjection->setChunkIndex(rightRadiusProjectionIndex);

    if (colliderA != colliderB)
    {
        if (!leftMax && rightMax)
        {
            removeCollision(colliderA, colliderB);
        }
        else if (leftMax && !rightMax)
        {
            emitCollision(colliderA, colliderB);
        }
    }

    if (leftChunk != rightChunk)
    {
        leftRadiusProjection->setChunk(rightChunk);
        rightRadiusProjection->setChunk(leftChunk);

        if (!isPrimary)
        {
            return;
        }

        if (!leftMax)
        {
            leftChunk->removeCheckpoint(colliderA);
        }
        else
        {
            leftChunk->addCheckpoint(colliderA);
        }

        if (!rightMax)
        {
            leftChunk->addCheckpoint(colliderB);
        }
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

void SegmentedIntervalList::addCollisionsForNewlyAddedProjection(BoundingRadiusProjectionProxy *lowerProjection,
                                                                 size_t lowerIndexInsideChunkWhereItWasInserted,
                                                                 BoundingRadiusProjectionProxy *upperProjection,
                                                                 size_t upperIndexInsideChunkWhereItWasInserted)
{
    this->processProjectionCollisions(lowerProjection, lowerIndexInsideChunkWhereItWasInserted, upperProjection,
                                      upperIndexInsideChunkWhereItWasInserted,
                                      [this](Collider *colliderA, Collider *colliderB) {
                                          owner->overlapBeginCheckpoint(colliderA, colliderB);
                                      });
}

void SegmentedIntervalList::removeCollisionsForRemovedProjection(BoundingRadiusProjectionProxy *lowerProjection,
                                                                 size_t lowerIndexInsideChunkWhereItWasRemoved,
                                                                 BoundingRadiusProjectionProxy *upperProjection,
                                                                 size_t upperIndexInsideChunkWhereItWasRemoved)
{
    this->processProjectionCollisions(lowerProjection, lowerIndexInsideChunkWhereItWasRemoved, upperProjection,
                                      upperIndexInsideChunkWhereItWasRemoved,
                                      [this](Collider *colliderA, Collider *colliderB) {
                                          owner->overlapEndCheckpoint(colliderA, colliderB);
                                      });
}

template <typename EmitFn>
void SegmentedIntervalList::processProjectionCollisions(BoundingRadiusProjectionProxy *lowerProjection,
                                                        size_t lowerIndexInsideChunk,
                                                        BoundingRadiusProjectionProxy *upperProjection,
                                                        size_t upperIndexInsideChunk, EmitFn &&emit)
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
            if (lowerChunk->getIsMaxima(i))
            {
                emit(collider, lowerArr[i]->getCollider());
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
            Collider *other = lowerArr[i]->getCollider();
            if (!lowerChunk->getIsMaxima(i))
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
        if (lowerChunk->getIsMaxima(i))
        {
            Collider *other = lowerArr[i]->getCollider();
            if (other != collider)
            {
                emit(collider, other);
            }
        }
    }

    for (LocalSortArray *currentChunk = lowerChunk->getRightChunk();
         currentChunk != nullptr && currentChunk != upperChunk; currentChunk = currentChunk->getRightChunk())
    {
        BoundingRadiusProjectionProxy **chunkArr = currentChunk->getArray();
        const size_t chunkSz = currentChunk->getSize();
        for (size_t i = 0; i < chunkSz; ++i)
        {
            if (!currentChunk->getIsMaxima(i))
            {
                Collider *other = chunkArr[i]->getCollider();
                if (other != collider)
                {
                    emit(collider, other);
                }
            }
        }
    }

    BoundingRadiusProjectionProxy **upperArr = upperChunk->getArray();
    for (size_t i = 0; i < upperIndexInsideChunk; ++i)
    {
        if (!upperChunk->getIsMaxima(i))
        {
            Collider *other = upperArr[i]->getCollider();
            if (other != collider)
            {
                emit(collider, other);
            }
        }
    }
}
