#include "physics/broadphase/grid.h"
#include "benchmark/benchmark.h"
#include <algorithm>
#include "core/constants.h"
#include <cstdio>
#include <oneapi/tbb/parallel_sort.h>
#include "physics/physicsmanager.h"
#include "physics/physicstaskcontext.h"

bool Grid::rangeFitsCellCache(const GridCellRange &range) noexcept
{
    if (!range.getIsValid())
    {
        return false;
    }
    const int64_t width = static_cast<int64_t>(range.maxX) - static_cast<int64_t>(range.minX) + 1;
    const int64_t height = static_cast<int64_t>(range.maxY) - static_cast<int64_t>(range.minY) + 1;
    return width > 0 && height > 0 && width * height <= static_cast<int64_t>(Collider::MAX_CACHED_GRID_CELLS);
}

Grid::Grid(PhysicsManager *owner)
    : owner(owner), cells(), candidateCollisions(), pendingNarrowPhasePairs(), potentiallyEmptyCells(),
      cellsWithOperations(), pendingColliderOperations(), activePairDeltaCollector()
{
}

Grid::~Grid()
{
}

bool Grid::removeGridCellIfEmpty(GridCell *cell)
{
    if (!cell->getAABB()->getIsEmpty())
    {
        return false;
    }

    return cells.eraseAndMaybeRecycle(cell->getCellKey(), MAX_RECYCLED_GRID_CELLS);
}

GridCell *Grid::findCell(const GridCellKey &key)
{
    return cells.find(key);
}

const GridCell *Grid::findCell(const GridCellKey &key) const
{
    return cells.find(key);
}

GridCell &Grid::getOrCreateCell(const GridCellKey &key)
{
    return cells.getOrCreate(key, this);
}

void Grid::addColliderInternal(Collider *collider, bool geometryPrepared)
{
    if (!geometryPrepared)
    {
        collider->prepareSyncFromCache();
    }
    collider->updateGridCellRange();
    const GridCellRange &cachedRange = collider->getGridCellRange();

    PLATFORMATOR_LOG("Adding collider %s to grid cells in range (%d, %d) to (%d, %d)\n",
                     collider->getGameObject()->getName().c_str(), cachedRange.minX, cachedRange.minY, cachedRange.maxX,
                     cachedRange.maxY);

    collider->cachedCellCount = 0;
    const bool canCache = rangeFitsCellCache(cachedRange);
    cachedRange.forEachCell([&](const GridCellKey &key) {
        GridCell &cell = getOrCreateCell(key);
        cell.queueAddCollider(collider);
        queueForOperations(&cell);
        if (canCache)
        {
            collider->cachedCells[collider->cachedCellCount++] = &cell;
        }
    });

    collider->setIsRegisteredInGrid(true);
}

void Grid::removeColliderInternal(Collider *collider)
{
    const GridCellRange &range = collider->getGridCellRange();

    PLATFORMATOR_LOG("Removing collider %s from grid cells in range (%d, %d) to (%d, %d)\n",
                     collider->getGameObject()->getName().c_str(), range.minX, range.minY, range.maxX, range.maxY);

    if (collider->cachedCellCount > 0)
    {
        for (uint8_t i = 0; i < collider->cachedCellCount; ++i)
        {
            GridCell *cell = collider->cachedCells[i];
            cell->queueRemoveCollider(collider);
            queueForEmptyCheck(cell);
            queueForOperations(cell);
        }
        collider->cachedCellCount = 0;
    }
    else
    {
        range.forEachCell([&](const GridCellKey &key) {
            if (GridCell *cell = findCell(key))
            {
                cell->queueRemoveCollider(collider);
                queueForEmptyCheck(cell);
                queueForOperations(cell);
            }
        });
    }

    collider->setIsRegisteredInGrid(false);
}

void Grid::expandActiveCellBounds(const GridCellRange &range) noexcept
{
    if (activeCellBoundsDirty)
    {
        return;
    }
    activeCellBounds.minX = std::min(activeCellBounds.minX, range.minX);
    activeCellBounds.maxX = std::max(activeCellBounds.maxX, range.maxX);
    activeCellBounds.minY = std::min(activeCellBounds.minY, range.minY);
    activeCellBounds.maxY = std::max(activeCellBounds.maxY, range.maxY);
}

void Grid::recomputeActiveCellBounds() const noexcept
{
    activeCellBounds = GridCellRange{};
    for (const auto &entry : cells)
    {
        const GridCellKey &key = entry.first;
        activeCellBounds.minX = std::min(activeCellBounds.minX, key.x);
        activeCellBounds.maxX = std::max(activeCellBounds.maxX, key.x);
        activeCellBounds.minY = std::min(activeCellBounds.minY, key.y);
        activeCellBounds.maxY = std::max(activeCellBounds.maxY, key.y);
    }
    activeCellBoundsDirty = false;
}

void Grid::dequeuePairFromNarrowPhase(const ColliderPair *pair)
{
    if (pair == nullptr || !pair->getIsQueuedForNarrowPhase())
    {
        return;
    }

    size_t removeIndex = pair->getNarrowPhaseQueueIndex();
    size_t lastIndex = pendingNarrowPhasePairs.size() - 1;

    if (removeIndex != lastIndex)
    {
        const ColliderPair *movedPair = pendingNarrowPhasePairs[lastIndex];
        pendingNarrowPhasePairs[removeIndex] = movedPair;
        movedPair->setNarrowPhaseQueueIndex(removeIndex);
    }

    pendingNarrowPhasePairs.pop_back();
    pair->setNarrowPhaseQueueIndex(SIZE_MAX);
}

void Grid::queuePairsForCollider(Collider *collider)
{
    const std::vector<const ColliderPair *> *touchingPairs = getTouchingPairs(collider);
    if (touchingPairs == nullptr)
    {
        return;
    }

    for (const ColliderPair *pair : *touchingPairs)
    {
        queuePairForNarrowPhase(pair);
    }
}

void Grid::queuePairForNarrowPhase(const ColliderPair *pair)
{
    if (pair == nullptr || pair->getIsQueuedForNarrowPhase())
    {
        return;
    }

    pair->setNarrowPhaseQueueIndex(pendingNarrowPhasePairs.size());
    pendingNarrowPhasePairs.push_back(pair);
}

const std::vector<const ColliderPair *> *Grid::getPendingNarrowPhasePairs() const
{
    return &pendingNarrowPhasePairs;
}

const std::vector<const ColliderPair *> *Grid::getTouchingPairs(Collider *collider) const
{
    if (collider == nullptr || collider->adjacentPairs.empty())
    {
        return nullptr;
    }

    return &collider->adjacentPairs;
}

void Grid::clearPendingNarrowPhasePairs()
{
    // Workers consumed their own indices. Their fixed pointer batch lives through the serial reduction.
    pendingNarrowPhasePairs.clear();
}

void Grid::recordPairDelta(Collider *colliderA, Collider *colliderB, int delta)
{
    if (delta == 0)
    {
        return;
    }

    activePairDeltaCollector.record(colliderA, colliderB, delta);
}

void Grid::applyPairWitnessDelta(Collider *colliderA, Collider *colliderB, int delta)
{
    if (delta == 0)
    {
        return;
    }

    const ColliderPair *pair = nullptr;
    if (delta < 0)
    {
        pair = candidateCollisions.find(colliderA, colliderB);
        if (pair == nullptr)
        {
            return;
        }
    }
    else
    {
        pair = candidateCollisions.findOrEmplace(colliderA, colliderB);
    }

    int previousWitnessCount = pair->getWitnessCount();
    if (delta > 0)
    {
        pair->addToWitnessCount(delta);

        if (previousWitnessCount != 0)
        {
            return;
        }

        Collider *objectA = pair->getObjectA();
        Collider *objectB = pair->getObjectB();

        std::vector<const ColliderPair *> &adjacencyA = objectA->adjacentPairs;
        pair->setAdjacencyIndexA(adjacencyA.size());
        adjacencyA.push_back(pair);

        std::vector<const ColliderPair *> &adjacencyB = objectB->adjacentPairs;
        pair->setAdjacencyIndexB(adjacencyB.size());
        adjacencyB.push_back(pair);

        queuePairForNarrowPhase(pair);
        return;
    }
    else
    {

        pair->addToWitnessCount(delta);

        if (pair->getWitnessCount() > 0)
        {
            return;
        }

        Collider *objectA = pair->getObjectA();
        Collider *objectB = pair->getObjectB();

        dequeuePairFromNarrowPhase(pair);

        const auto detachAdjacency = [pair](Collider *endpoint, size_t removeIndex) {
            std::vector<const ColliderPair *> &pairs = endpoint->adjacentPairs;
            if (removeIndex >= pairs.size())
            {
                return;
            }
            const size_t lastIndex = pairs.size() - 1;
            if (removeIndex != lastIndex)
            {
                const ColliderPair *movedPair = pairs[lastIndex];
                pairs[removeIndex] = movedPair;
                if (movedPair->getObjectA() == endpoint)
                {
                    movedPair->setAdjacencyIndexA(removeIndex);
                }
                else
                {
                    movedPair->setAdjacencyIndexB(removeIndex);
                }
            }
            pairs.pop_back();
        };

        detachAdjacency(objectA, pair->getAdjacencyIndexA());
        detachAdjacency(objectB, pair->getAdjacencyIndexB());

        if (owner)
            owner->removePair(pair);
        candidateCollisions.erase(colliderA, colliderB);
    }
}

void Grid::applyCollectedPairDeltas()
{
    pairDeltaScratch.clear();
    for (auto &local : activePairDeltaCollector.localPairDeltas)
    {
        pairDeltaScratch.insert(pairDeltaScratch.end(), local.begin(), local.end());
        local.clear();
    }
    const auto deltaLess = [](const PairDelta &a, const PairDelta &b) {
        if (a.sortKey != b.sortKey)
            return a.sortKey < b.sortKey;
        if (a.colliderA != b.colliderA)
            return a.colliderA < b.colliderA;
        return a.colliderB < b.colliderB;
    };
    if (pairDeltaScratch.size() >= PAIR_DELTA_PARALLEL_SORT_THRESHOLD)
    {
        tbb::parallel_sort(pairDeltaScratch.begin(), pairDeltaScratch.end(), deltaLess);
    }
    else
    {
        std::sort(pairDeltaScratch.begin(), pairDeltaScratch.end(), deltaLess);
    }
    for (size_t i = 0; i < pairDeltaScratch.size();)
    {
        const auto first = pairDeltaScratch[i];
        int delta = 0;
        do
        {
            delta += pairDeltaScratch[i++].delta;
        } while (i < pairDeltaScratch.size() && pairDeltaScratch[i].colliderA == first.colliderA &&
                 pairDeltaScratch[i].colliderB == first.colliderB);
        if (delta)
            applyPairWitnessDelta(first.colliderA, first.colliderB, delta);
    }
}

const Grid::CellMap &Grid::getCells() const
{
    return cells;
}

bool Grid::containsCellPair(Collider *colliderA, Collider *colliderB) const
{
    const AABBPair pair(colliderA, colliderB);

    for (const auto &cellEntry : cells)
    {
        const AABB *aabb = cellEntry.second.getAABB();
        if (aabb->pairsWithBothAxisOverlapping.contains(pair))
        {
            return true;
        }
    }

    return false;
}

size_t Grid::getCandidatePairCount() const
{
    return candidateCollisions.size();
}

void Grid::flushDeferredPairDeltas()
{
    applyCollectedPairDeltas();
}

void Grid::syncCollider(Collider *collider)
{
    syncColliderInternal(collider, false);
}

void Grid::syncColliderInternal(Collider *collider, bool geometryPrepared)
{
    const bool geometry = collider->dirty & Collider::Geometry;
    const bool filter = collider->dirty & Collider::Filter;
    if (geometry && !geometryPrepared)
        collider->prepareSyncFromCache();
    if (!geometry && !filter)
        return;
    const auto oldRange = collider->getGridCellRange();
    const auto newRange = collider->getGridCellRangeCache();
    collider->updateGridCellRange();

    if (!filter && oldRange == newRange)
    {
        if (collider->cachedCellCount > 0)
        {
            for (uint8_t i = 0; i < collider->cachedCellCount; ++i)
            {
                GridCell *cell = collider->cachedCells[i];
                cell->queueSyncCollider(collider);
                queueForOperations(cell);
            }
            return;
        }

        newRange.forEachCell([&](const GridCellKey &key) {
            if (auto *cell = findCell(key))
            {
                cell->queueSyncCollider(collider);
                queueForOperations(cell);
            }
        });
        return;
    }

    GridCell *oldCached[Collider::MAX_CACHED_GRID_CELLS] = {};
    const uint8_t oldCachedCount = collider->cachedCellCount;
    for (uint8_t i = 0; i < oldCachedCount; ++i)
    {
        oldCached[i] = collider->cachedCells[i];
    }
    collider->cachedCellCount = 0;
    const bool canCache = rangeFitsCellCache(newRange);

    const auto findOldCell = [&](const GridCellKey &key) -> GridCell * {
        for (uint8_t i = 0; i < oldCachedCount; ++i)
        {
            if (oldCached[i]->getCellKey() == key)
            {
                return oldCached[i];
            }
        }
        return findCell(key);
    };

    const auto remove = [&](const GridCellKey &key) {
        if (auto *cell = findOldCell(key))
        {
            cell->queueRemoveCollider(collider);
            queueForEmptyCheck(cell);
            queueForOperations(cell);
        }
    };
    const auto add = [&](const GridCellKey &key) {
        auto &cell = getOrCreateCell(key);
        cell.queueAddCollider(collider);
        queueForOperations(&cell);
        if (canCache)
        {
            collider->cachedCells[collider->cachedCellCount++] = &cell;
        }
    };
    if (filter)
    {
        if (oldCachedCount > 0)
        {
            for (uint8_t i = 0; i < oldCachedCount; ++i)
            {
                GridCell *cell = oldCached[i];
                cell->queueRemoveCollider(collider);
                queueForEmptyCheck(cell);
                queueForOperations(cell);
            }
        }
        else
        {
            oldRange.forEachCell(remove);
        }
        newRange.forEachCell(add);
    }
    else
    {
        oldRange.forEachDifference(newRange, remove);
        newRange.forEachDifference(oldRange, add);
        newRange.forEachSame(oldRange, [&](const GridCellKey &key) {
            if (auto *cell = findOldCell(key))
            {
                cell->queueSyncCollider(collider);
                queueForOperations(cell);
                if (canCache)
                {
                    collider->cachedCells[collider->cachedCellCount++] = cell;
                }
            }
        });
    }
}

void Grid::queueForEmptyCheck(GridCell *cell)
{
    if (!cell->markQueuedForEmpty())
    {
        return;
    }

    potentiallyEmptyCells.push_back(cell);
}

void Grid::flushPendingCellUpdates()
{
    if (pendingColliderOperations.empty() && cellsWithOperations.empty() && potentiallyEmptyCells.empty())
        return;

    platformator_detail::runPhysicsWork([&] { flushPendingWork(); });
}

void Grid::flushPendingWork()
{
    const auto prepareColliderGeometry = [](Collider *collider) {
        if (collider->desiredInGrid && (!collider->getIsRegisteredInGrid() || (collider->dirty & Collider::Geometry)))
        {
            collider->prepareSyncFromCache();
        }
    };
    if (pendingColliderOperations.size() < GRID_GEOMETRY_PREP_PARALLEL_THRESHOLD)
    {
        for (auto *collider : pendingColliderOperations)
            prepareColliderGeometry(collider);
    }
    else
    {
        tbb::parallel_for(size_t(0), pendingColliderOperations.size(),
                          [&](size_t i) { prepareColliderGeometry(pendingColliderOperations[i]); });
    }
    for (auto *collider : pendingColliderOperations)
    {
        if (!collider->desiredInGrid)
        {
            if (collider->getIsRegisteredInGrid())
                removeColliderInternal(collider);
        }
        else if (!collider->getIsRegisteredInGrid())
            addColliderInternal(collider, true);
        else if (collider->dirty)
            syncColliderInternal(collider, true);
    }
    const bool runCellsParallel =
        cellsWithOperations.size() >= GRID_CELL_FLUSH_PARALLEL_THRESHOLD ||
        (cellsWithOperations.size() >= GRID_CELL_FLUSH_DENSE_MIN_CELLS &&
         pendingColliderOperations.size() >= GRID_UPDATE_PARALLEL_THRESHOLD);
    if (!runCellsParallel)
    {
        for (auto *cell : cellsWithOperations)
            cell->flushPendingOperations();
    }
    else
    {
        tbb::parallel_for(size_t(0), cellsWithOperations.size(),
                          [&](size_t i) { cellsWithOperations[i]->flushPendingOperations(); });
    }
    cellsWithOperations.clear();
    applyCollectedPairDeltas();
    for (auto *cell : potentiallyEmptyCells)
    {
        const GridCellKey key = cell->getCellKey();
        const bool wasBoundary =
            !activeCellBoundsDirty &&
            (key.x <= activeCellBounds.minX || key.x >= activeCellBounds.maxX ||
             key.y <= activeCellBounds.minY || key.y >= activeCellBounds.maxY);
        cell->clearQueuedForEmpty();
        if (removeGridCellIfEmpty(cell) && wasBoundary)
        {
            activeCellBoundsDirty = true;
        }
    }
    potentiallyEmptyCells.clear();
    size_t totalAdjacentPairs = 0;
    for (auto *collider : pendingColliderOperations)
    {
        if (collider->desiredInGrid)
        {
            totalAdjacentPairs += collider->adjacentPairs.size();
            if (!activeCellBoundsDirty && collider->getIsRegisteredInGrid())
                expandActiveCellBounds(collider->getGridCellRange());
        }
        if (owner && (collider->dirty & Collider::Response))
            owner->refreshResponse(collider);
    }

    const bool runPairQueueParallel =
        totalAdjacentPairs >= GRID_PAIR_QUEUE_PARALLEL_THRESHOLD &&
        tbb::global_control::active_value(tbb::global_control::max_allowed_parallelism) > 1 &&
        tbb::this_task_arena::max_concurrency() > 1;
    if (!runPairQueueParallel)
    {
        for (auto *collider : pendingColliderOperations)
        {
            if (collider->desiredInGrid)
            {
                queuePairsForCollider(collider);
            }
            collider->gridQueueIndex = SIZE_MAX;
            collider->dirty = 0;
        }
    }
    else
    {
        const size_t opCount = pendingColliderOperations.size();
        const size_t targetPerChunk = std::max<size_t>(2048, (totalAdjacentPairs + 31) / 32);
        pairQueueChunkRanges.clear();
        size_t chunkStart = 0;
        size_t chunkWeight = 0;
        for (size_t idx = 0; idx < opCount; ++idx)
        {
            Collider *collider = pendingColliderOperations[idx];
            if (collider->desiredInGrid)
            {
                chunkWeight += collider->adjacentPairs.size();
            }
            if (chunkWeight >= targetPerChunk && pairQueueChunkRanges.size() < 63)
            {
                pairQueueChunkRanges.emplace_back(chunkStart, idx + 1);
                chunkStart = idx + 1;
                chunkWeight = 0;
            }
        }
        if (chunkStart < opCount)
        {
            pairQueueChunkRanges.emplace_back(chunkStart, opCount);
        }

        const size_t chunkCount = pairQueueChunkRanges.size();
        if (pairQueueChunkScratch.size() < chunkCount)
        {
            pairQueueChunkScratch.resize(chunkCount);
        }

        // Preserve exact serial insertion order by claiming each pair at the smallest
        // queued endpoint gridQueueIndex without writing to ColliderPair in pass 1.
        tbb::parallel_for(size_t(0), chunkCount, [&](size_t chunkIdx) {
            auto &local = pairQueueChunkScratch[chunkIdx];
            local.clear();
            const auto [start, end] = pairQueueChunkRanges[chunkIdx];
            for (size_t idx = start; idx < end; ++idx)
            {
                Collider *collider = pendingColliderOperations[idx];
                if (!collider->desiredInGrid)
                {
                    continue;
                }
                for (const ColliderPair *pair : collider->adjacentPairs)
                {
                    if (pair == nullptr || pair->getIsQueuedForNarrowPhase())
                    {
                        continue;
                    }
                    const Collider *other =
                        pair->getObjectA() == collider ? pair->getObjectB() : pair->getObjectA();
                    if (!other->desiredInGrid || other->gridQueueIndex > idx)
                    {
                        local.push_back(pair);
                    }
                }
            }
        });

        pairQueueChunkOffsets.resize(chunkCount);
        size_t writeOffset = pendingNarrowPhasePairs.size();
        for (size_t chunkIdx = 0; chunkIdx < chunkCount; ++chunkIdx)
        {
            pairQueueChunkOffsets[chunkIdx] = writeOffset;
            writeOffset += pairQueueChunkScratch[chunkIdx].size();
        }
        if (writeOffset > pendingNarrowPhasePairs.size())
        {
            pendingNarrowPhasePairs.resize(writeOffset);
            tbb::parallel_for(size_t(0), chunkCount, [&](size_t chunkIdx) {
                const auto &local = pairQueueChunkScratch[chunkIdx];
                size_t outIdx = pairQueueChunkOffsets[chunkIdx];
                for (const ColliderPair *pair : local)
                {
                    pair->setNarrowPhaseQueueIndex(outIdx);
                    pendingNarrowPhasePairs[outIdx] = pair;
                    ++outIdx;
                }
            });
        }

        for (auto *collider : pendingColliderOperations)
        {
            collider->gridQueueIndex = SIZE_MAX;
            collider->dirty = 0;
        }
    }
    pendingColliderOperations.clear();
}

void Grid::clearAll()
{
    for (Collider *collider : pendingColliderOperations)
    {
        collider->setIsRegisteredInGrid(false);
        collider->desiredInGrid = false;
        collider->gridQueueIndex = SIZE_MAX;
        collider->cachedCellCount = 0;
    }
    for (auto &cellEntry : cells)
    {
        cellEntry.second.getAABB()->forEachRegisteredCollider([](Collider *collider) {
            collider->cachedCellCount = 0;
        });
    }
    candidateCollisions.forEach([](const ColliderPair &pair) {
        pair.getObjectA()->adjacentPairs.clear();
        pair.getObjectB()->adjacentPairs.clear();
    });
    cells.clear();
    candidateCollisions.clear();
    pendingNarrowPhasePairs.clear();
    potentiallyEmptyCells.clear();
    cellsWithOperations.clear();
    pendingColliderOperations.clear();
    pairDeltaScratch.clear();
    pairQueueChunkRanges.clear();
    pairQueueChunkOffsets.clear();
    for (auto &chunk : pairQueueChunkScratch)
    {
        chunk.clear();
    }
    for (auto &local : activePairDeltaCollector.localPairDeltas)
    {
        local.clear();
    }
    activeCellBounds = GridCellRange{};
    activeCellBoundsDirty = false;
}

void Grid::queueForOperations(GridCell *cell)
{
    if (!cell->markQueuedForOperations())
    {
        return;
    }

    cellsWithOperations.push_back(cell);
}

void Grid::queueAddCollider(Collider *collider)
{
    collider->desiredInGrid = true;
    collider->dirty |= Collider::Geometry;
    queueCollider(collider);
    PLATFORMATOR_BENCH_ADD_COUNTER(QueuedAddCount, 1);
}

void Grid::queueRemoveCollider(Collider *collider)
{
    collider->desiredInGrid = false;
    queueCollider(collider);
    PLATFORMATOR_BENCH_ADD_COUNTER(QueuedRemoveCount, 1);
}

void Grid::queueSyncCollider(Collider *collider)
{
    if (queueCollider(collider))
    {
        PLATFORMATOR_BENCH_ADD_COUNTER(QueuedSyncCount, 1);
    }
}
bool Grid::queueCollider(Collider *collider)
{
    if (collider->gridQueueIndex != SIZE_MAX)
        return false;
    collider->gridQueueIndex = pendingColliderOperations.size();
    pendingColliderOperations.push_back(collider);
    return true;
}
