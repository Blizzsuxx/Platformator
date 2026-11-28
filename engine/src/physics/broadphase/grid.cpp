#include "physics/broadphase/grid.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/parallel_sort.h>

#include "benchmark/benchmark.h"
#include "benchmark/workprobe.h"
#include "core/constants.h"
#include "core/membership.h"
#include "physics/physicsmanager.h"
#include "physics/physicstaskcontext.h"

namespace
{
template <typename Fn> void forEachIndexParallelOrSerial(size_t count, size_t parallelThreshold, Fn &&fn)
{
    if (count < parallelThreshold || !PhysicsManager::hasParallelPhysicsWorkers())
    {
        for (size_t i = 0; i < count; ++i)
        {
            fn(i);
        }
    }
    else
    {
        tbb::parallel_for(size_t(0), count, std::forward<Fn>(fn));
    }
}
} // namespace

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
      cellsWithOperations(), pendingColliderOperations(), activePairDeltaCollector(), cellSize(GRID_CELL_SIZE),
      resizeCooldown(0)
{
}

float Grid::selectQuantizedCellSize(const std::vector<Collider *> &colliders)
{
    constexpr std::array<float, 7> allowedCellSizes{12.5f, 25.0f, 50.0f, 100.0f, 200.0f, 400.0f, 800.0f};
    constexpr size_t maxSamples = 512;

    std::array<float, maxSamples> dynamicExtents;
    std::array<float, maxSamples> allExtents;
    size_t dynamicCount = 0;
    size_t allCount = 0;
    const size_t sampleCount = std::min(colliders.size(), maxSamples);
    if (sampleCount < 16)
        return GRID_CELL_SIZE;
    const size_t intervals = sampleCount - 1;
    const size_t stride = (colliders.size() - 1) / intervals;
    const size_t remainder = (colliders.size() - 1) % intervals;
    PLATFORMATOR_WORK_ADD(gridSizingSampleVisits, sampleCount);

    for (size_t sample = 0; sample < sampleCount; ++sample)
    {
        // Spread the samples across the entire collection, including both ends.
        const size_t i = sample * stride + sample * remainder / intervals;
        const Collider *c = colliders[i];
        if (c == nullptr || (!c->desiredInGrid && !c->getIsRegisteredInGrid()))
        {
            continue;
        }
        const float w = c->getTightMaxX() - c->getTightMinX();
        const float h = c->getTightMaxY() - c->getTightMinY();
        const float extent = std::max(w, h);
        if (!std::isfinite(extent) || extent <= 0.0f)
        {
            continue;
        }
        allExtents[allCount++] = extent;
        if (c->hasNonStaticRigidbody())
        {
            dynamicExtents[dynamicCount++] = extent;
        }
    }

    float *sampleData = dynamicCount >= 16 ? dynamicExtents.data() : allExtents.data();
    const size_t count = dynamicCount >= 16 ? dynamicCount : allCount;
    if (count < 16)
    {
        return GRID_CELL_SIZE;
    }

    const size_t mid = count / 2;
    std::nth_element(sampleData, sampleData + mid, sampleData + count);
    const float target = sampleData[mid] * 7.1f;

    constexpr float halfOctave = 1.41421356f;
    for (size_t i = 0; i + 1 < allowedCellSizes.size(); ++i)
    {
        if (target < allowedCellSizes[i] * halfOctave)
        {
            return allowedCellSizes[i];
        }
    }
    return allowedCellSizes.back();
}

void Grid::flushDirtyCells(size_t operationWeight)
{
    if (cellsWithOperations.empty())
    {
        return;
    }
    const bool runCellsParallel =
        PhysicsManager::hasParallelPhysicsWorkers() &&
        (cellsWithOperations.size() >= GRID_CELL_FLUSH_PARALLEL_THRESHOLD ||
         (cellsWithOperations.size() >= GRID_CELL_FLUSH_DENSE_MIN_CELLS &&
          operationWeight >= GRID_UPDATE_PARALLEL_THRESHOLD));
    if (!runCellsParallel)
    {
        for (auto *cell : cellsWithOperations)
        {
            cell->flushPendingOperations();
        }
    }
    else
    {
        tbb::parallel_for(size_t(0), cellsWithOperations.size(),
                          [&](size_t i) { cellsWithOperations[i]->flushPendingOperations(); });
    }
    cellsWithOperations.clear();
}

void Grid::rebuildGridForCellSize(float newCellSize)
{
    if (owner == nullptr || newCellSize == cellSize)
    {
        return;
    }

    std::vector<Collider *> registered;
    const auto &allColliders = owner->getColliders();
    registered.reserve(allColliders.size());
    for (Collider *collider : allColliders)
    {
        if (collider != nullptr && collider->getIsRegisteredInGrid())
        {
            registered.push_back(collider);
        }
    }
    if (registered.empty())
    {
        cellSize = newCellSize;
        return;
    }

    // Cancel old cell witnesses so matching pairs survive intact with zero enter/exit churn.
    candidateCollisions.forEach([this](const ColliderPair &pair) {
        recordPairDelta(pair.getObjectA(), pair.getObjectB(), -pair.getWitnessCount());
    });

    for (Collider *collider : registered)
    {
        collider->cachedCellCount = 0;
    }
    cells.clear();
    potentiallyEmptyCells.clear();
    cellsWithOperations.clear();
    activeCellBounds = GridCellRange{};
    activeCellBoundsDirty = false;
    cellSize = newCellSize;

    forEachIndexParallelOrSerial(registered.size(), GRID_GEOMETRY_PREP_PARALLEL_THRESHOLD,
                                 [&](size_t i) { registered[i]->calculateGridCellRange(cellSize); });

    for (Collider *collider : registered)
    {
        addColliderInternal(collider);
    }

    flushDirtyCells(registered.size());
    applyCollectedPairDeltas();
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

void Grid::addColliderInternal(Collider *collider)
{
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
    if (!activeCellBoundsDirty)
    {
        expandActiveCellBounds(cachedRange);
    }
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

    collider->invalidateFatBounds();
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

void Grid::dequeuePairFromNarrowPhase(ColliderPair *pair)
{
    if (pair == nullptr || !pair->getIsQueuedForNarrowPhase())
    {
        return;
    }

    swapRemoveIndexed(
        pendingNarrowPhasePairs, pair, [](const ColliderPair *p) { return p->getNarrowPhaseQueueIndex(); },
        [](ColliderPair *p, size_t idx) { p->setNarrowPhaseQueueIndex(idx); });
}

namespace
{
inline bool pairNeedsNarrowPhaseCheck(const ColliderPair *pair) noexcept
{
    return pair->getCollision() != nullptr || pair->getObjectA()->overlapsTightBounds(*pair->getObjectB());
}
} // namespace

void Grid::queuePairsForCollider(Collider *collider)
{
    const std::vector<ColliderPair *> *touchingPairs = getTouchingPairs(collider);
    if (touchingPairs == nullptr)
    {
        return;
    }

    for (ColliderPair *pair : *touchingPairs)
    {
        if (pair != nullptr && !pair->getIsQueuedForNarrowPhase() && pairNeedsNarrowPhaseCheck(pair))
        {
            queuePairForNarrowPhase(pair);
        }
    }
}

void Grid::queuePairForNarrowPhase(ColliderPair *pair)
{
    if (pair == nullptr || pair->getIsQueuedForNarrowPhase())
    {
        return;
    }

    pair->setNarrowPhaseQueueIndex(pendingNarrowPhasePairs.size());
    pendingNarrowPhasePairs.push_back(pair);
}

const std::vector<ColliderPair *> *Grid::getPendingNarrowPhasePairs() const
{
    return &pendingNarrowPhasePairs;
}

const std::vector<ColliderPair *> *Grid::getTouchingPairs(Collider *collider) const
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

    ColliderPair *pair = nullptr;
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

        auto &adjacencyA = objectA->adjacentPairs;
        pair->setAdjacencyIndexA(adjacencyA.size());
        adjacencyA.push_back(pair);

        auto &adjacencyB = objectB->adjacentPairs;
        pair->setAdjacencyIndexB(adjacencyB.size());
        adjacencyB.push_back(pair);

        if (pairNeedsNarrowPhaseCheck(pair))
        {
            queuePairForNarrowPhase(pair);
        }
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

        const auto detachAdjacency = [](Collider *endpoint, size_t removeIndex) {
            auto &pairs = endpoint->adjacentPairs;
            if (removeIndex >= pairs.size())
            {
                return;
            }
            const size_t lastIndex = pairs.size() - 1;
            if (removeIndex != lastIndex)
            {
                ColliderPair *movedPair = pairs[lastIndex];
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
    size_t totalDeltas = 0;
    for (const auto &local : activePairDeltaCollector.localPairDeltas)
    {
        totalDeltas += local.size();
    }
    if (totalDeltas == 0)
    {
        return;
    }
    scratch.pairDeltas.clear();
    if (scratch.pairDeltas.capacity() < totalDeltas)
    {
        scratch.pairDeltas.reserve(totalDeltas);
    }
    for (auto &local : activePairDeltaCollector.localPairDeltas)
    {
        scratch.pairDeltas.insert(scratch.pairDeltas.end(), local.begin(), local.end());
        local.clear();
    }
    const auto deltaLess = [](const PairDelta &a, const PairDelta &b) {
        if (a.sortKey != b.sortKey)
            return a.sortKey < b.sortKey;
        if (a.colliderA != b.colliderA)
            return a.colliderA < b.colliderA;
        return a.colliderB < b.colliderB;
    };
    if (scratch.pairDeltas.size() >= PAIR_DELTA_PARALLEL_SORT_THRESHOLD && PhysicsManager::hasParallelPhysicsWorkers())
    {
        tbb::parallel_sort(scratch.pairDeltas.begin(), scratch.pairDeltas.end(), deltaLess);
    }
    else
    {
        std::sort(scratch.pairDeltas.begin(), scratch.pairDeltas.end(), deltaLess);
    }
    for (size_t i = 0; i < scratch.pairDeltas.size();)
    {
        const auto first = scratch.pairDeltas[i];
        int delta = 0;
        do
        {
            delta += scratch.pairDeltas[i++].delta;
        } while (i < scratch.pairDeltas.size() && scratch.pairDeltas[i].colliderA == first.colliderA &&
                 scratch.pairDeltas[i].colliderB == first.colliderB);
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
    const platformator_detail::CanonicalColliderPairKey pair(colliderA, colliderB);

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

void Grid::syncColliderInternal(Collider *collider)
{
    const bool filter = (collider->dirty & Collider::Filter) != 0;
    const auto oldRange = collider->getGridCellRange();
    const auto newRange = collider->getGridCellRangeCache();
    collider->updateGridCellRange();
    if (!activeCellBoundsDirty)
    {
        expandActiveCellBounds(newRange);
    }

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

// ============================================================================
// Deferred Flush Pipeline
// ============================================================================

size_t Grid::preparePendingColliders(bool hasWorkers)
{
    const size_t opCount = pendingColliderOperations.size();
    const bool runPrepParallel = hasWorkers && opCount >= GRID_GEOMETRY_PREP_PARALLEL_THRESHOLD;
    const size_t prepChunkCount = runPrepParallel ? std::min<size_t>(32, (opCount + 63) / 64) : 1;
    if (scratch.structuralWorkChunks.size() < prepChunkCount)
    {
        scratch.structuralWorkChunks.resize(prepChunkCount);
    }

    const auto prepRange = [&](size_t start, size_t end, std::vector<Collider *> &structuralOut) {
        structuralOut.clear();
        for (size_t i = start; i < end; ++i)
        {
            if (i + 4 < end)
            {
                platformator_detail::prefetchRead(pendingColliderOperations[i + 4]);
            }
            Collider *collider = pendingColliderOperations[i];
            const bool registered = collider->getIsRegisteredInGrid();
            if (collider->desiredInGrid && (!registered || (collider->dirty & Collider::Geometry)))
            {
                collider->prepareSyncFromCache(cellSize);
            }
            if (!collider->desiredInGrid || !registered || collider->broadPhaseSyncNeeded ||
                (collider->dirty & (Collider::Filter | Collider::Response)) != 0)
            {
                structuralOut.push_back(collider);
            }
        }
    };

    if (!runPrepParallel)
    {
        prepRange(0, opCount, scratch.structuralWorkChunks[0]);
    }
    else
    {
        const size_t base = opCount / prepChunkCount;
        const size_t rem = opCount % prepChunkCount;
        tbb::parallel_for(size_t(0), prepChunkCount, [&](size_t chunkIdx) {
            const size_t start = chunkIdx * base + std::min(chunkIdx, rem);
            const size_t end = start + base + (chunkIdx < rem ? 1 : 0);
            prepRange(start, end, scratch.structuralWorkChunks[chunkIdx]);
        });
    }

    if (cells.empty() && opCount >= 32)
    {
        const float desiredCellSize = selectQuantizedCellSize(pendingColliderOperations);
        if (desiredCellSize != cellSize)
        {
            cellSize = desiredCellSize;
            forEachIndexParallelOrSerial(opCount, GRID_GEOMETRY_PREP_PARALLEL_THRESHOLD, [&](size_t i) {
                Collider *collider = pendingColliderOperations[i];
                if (collider->desiredInGrid)
                {
                    collider->calculateGridCellRange(cellSize);
                }
            });
        }
    }
    return prepChunkCount;
}

void Grid::applyStructuralColliderOps(size_t prepChunkCount)
{
    scratch.responseDirtyColliders.clear();
    for (size_t c = 0; c < prepChunkCount; ++c)
    {
        for (Collider *collider : scratch.structuralWorkChunks[c])
        {
            if (!collider->desiredInGrid)
            {
                if (collider->getIsRegisteredInGrid())
                    removeColliderInternal(collider);
            }
            else if (!collider->getIsRegisteredInGrid())
            {
                addColliderInternal(collider);
                if (owner && (collider->dirty & Collider::Response))
                    scratch.responseDirtyColliders.push_back(collider);
            }
            else
            {
                if (collider->broadPhaseSyncNeeded || (collider->dirty & Collider::Filter))
                    syncColliderInternal(collider);
                if (owner && (collider->dirty & Collider::Response))
                    scratch.responseDirtyColliders.push_back(collider);
            }
        }
    }
}

void Grid::reclaimEmptyCellsAndMaybeResize()
{
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

    if (resizeCooldown > 0)
    {
        --resizeCooldown;
    }
    else if (owner != nullptr && !cells.empty())
    {
        const auto &allColliders = owner->getColliders();
        const size_t totalColliders = allColliders.size();
        if (totalColliders >= 128 && totalColliders >= cells.size() * 96)
        {
            const float desiredCellSize = selectQuantizedCellSize(allColliders);
            resizeCooldown = 120;
            if (desiredCellSize <= cellSize * 0.5f)
            {
                rebuildGridForCellSize(desiredCellSize);
            }
        }
    }
}

void Grid::queueNarrowPhasePairsSerial()
{
    const size_t opCount = pendingColliderOperations.size();
    for (size_t i = 0; i < opCount; ++i)
    {
        if (i + 4 < opCount)
        {
            platformator_detail::prefetchRead(pendingColliderOperations[i + 4]);
        }
        Collider *collider = pendingColliderOperations[i];
        if (collider->desiredInGrid)
        {
            queuePairsForCollider(collider);
        }
        collider->gridQueueIndex = Collider::INVALID_INDEX;
        collider->dirty = 0;
    }
}

void Grid::queueNarrowPhasePairsParallel(bool useUniformChunks, size_t totalAdjacentPairs)
{
    const size_t opCount = pendingColliderOperations.size();
    scratch.pairQueueRanges.clear();
    if (useUniformChunks)
    {
        const size_t chunkCount = std::min<size_t>(32, (opCount + 127) / 128);
        const size_t base = opCount / chunkCount;
        const size_t rem = opCount % chunkCount;
        size_t cursor = 0;
        for (size_t c = 0; c < chunkCount; ++c)
        {
            const size_t len = base + (c < rem ? 1 : 0);
            scratch.pairQueueRanges.emplace_back(cursor, cursor + len);
            cursor += len;
        }
    }
    else
    {
        const size_t targetPerChunk = std::max<size_t>(2048, (totalAdjacentPairs + 31) / 32);
        size_t chunkStart = 0;
        size_t chunkWeight = 0;
        for (size_t idx = 0; idx < opCount; ++idx)
        {
            Collider *collider = pendingColliderOperations[idx];
            if (collider->desiredInGrid)
            {
                chunkWeight += collider->adjacentPairs.size();
            }
            if (chunkWeight >= targetPerChunk && scratch.pairQueueRanges.size() < 63)
            {
                scratch.pairQueueRanges.emplace_back(chunkStart, idx + 1);
                chunkStart = idx + 1;
                chunkWeight = 0;
            }
        }
        if (chunkStart < opCount)
        {
            scratch.pairQueueRanges.emplace_back(chunkStart, opCount);
        }
    }

    const size_t chunkCount = scratch.pairQueueRanges.size();
    if (scratch.pairQueueChunks.size() < chunkCount)
    {
        scratch.pairQueueChunks.resize(chunkCount);
    }

    // Pass 1: preserve exact serial insertion order by claiming each pair at the smallest
    // queued endpoint gridQueueIndex without writing to ColliderPair.
    tbb::parallel_for(size_t(0), chunkCount, [&](size_t chunkIdx) {
        auto &local = scratch.pairQueueChunks[chunkIdx];
        local.clear();
        const auto [start, end] = scratch.pairQueueRanges[chunkIdx];
        for (size_t idx = start; idx < end; ++idx)
        {
            if (idx + 2 < end)
            {
                platformator_detail::prefetchRead(pendingColliderOperations[idx + 2]);
            }
            Collider *collider = pendingColliderOperations[idx];
            if (!collider->desiredInGrid || collider->adjacentPairs.empty())
            {
                continue;
            }
            const auto &adj = collider->adjacentPairs;
            const size_t adjCount = adj.size();
            const float cMinX = collider->getTightMinX();
            const float cMaxX = collider->getTightMaxX();
            const float cMinY = collider->getTightMinY();
            const float cMaxY = collider->getTightMaxY();
            for (size_t j = 0; j < adjCount; ++j)
            {
                if (j + 4 < adjCount)
                {
                    platformator_detail::prefetchRead(adj[j + 4]);
                }
                ColliderPair *pair = adj[j];
                if (pair == nullptr || pair->getIsQueuedForNarrowPhase())
                {
                    continue;
                }
                const Collider *other = pair->getObjectA() == collider ? pair->getObjectB() : pair->getObjectA();
                if ((!other->desiredInGrid || static_cast<size_t>(other->gridQueueIndex) > idx) &&
                    (pair->getCollision() != nullptr || other->overlapsTightBounds(cMinX, cMaxX, cMinY, cMaxY)))
                {
                    local.push_back(pair);
                }
            }
        }
    });

    scratch.pairQueueOffsets.resize(chunkCount);
    size_t writeOffset = pendingNarrowPhasePairs.size();
    for (size_t chunkIdx = 0; chunkIdx < chunkCount; ++chunkIdx)
    {
        scratch.pairQueueOffsets[chunkIdx] = writeOffset;
        writeOffset += scratch.pairQueueChunks[chunkIdx].size();
    }
    const bool hasWrites = writeOffset > pendingNarrowPhasePairs.size();
    if (hasWrites)
    {
        pendingNarrowPhasePairs.resize(writeOffset);
    }

    // Pass 2: scatter narrow-phase pairs AND reset gridQueueIndex/dirty in parallel (gridQueueIndex no longer read).
    tbb::parallel_for(size_t(0), chunkCount, [&](size_t chunkIdx) {
        if (hasWrites)
        {
            const auto &local = scratch.pairQueueChunks[chunkIdx];
            const size_t localCount = local.size();
            size_t outIdx = scratch.pairQueueOffsets[chunkIdx];
            for (size_t j = 0; j < localCount; ++j)
            {
                if (j + 4 < localCount)
                {
                    platformator_detail::prefetchWrite(local[j + 4]);
                }
                ColliderPair *pair = local[j];
                pair->setNarrowPhaseQueueIndex(outIdx);
                pendingNarrowPhasePairs[outIdx] = pair;
                ++outIdx;
            }
        }
        const auto [start, end] = scratch.pairQueueRanges[chunkIdx];
        for (size_t idx = start; idx < end; ++idx)
        {
            Collider *collider = pendingColliderOperations[idx];
            collider->gridQueueIndex = Collider::INVALID_INDEX;
            collider->dirty = 0;
        }
    });
}

void Grid::flushPendingWork()
{
    const size_t opCount = pendingColliderOperations.size();
    const bool hasWorkers = PhysicsManager::hasParallelPhysicsWorkers();

    const size_t prepChunkCount = preparePendingColliders(hasWorkers);
    applyStructuralColliderOps(prepChunkCount);

    if (!cellsWithOperations.empty())
    {
        flushDirtyCells(opCount);
        applyCollectedPairDeltas();
    }
    reclaimEmptyCellsAndMaybeResize();

    if (owner && !scratch.responseDirtyColliders.empty())
    {
        for (auto *collider : scratch.responseDirtyColliders)
        {
            owner->refreshResponse(collider);
        }
        scratch.responseDirtyColliders.clear();
    }

    bool runPairQueueParallel = false;
    bool useUniformChunks = false;
    size_t totalAdjacentPairs = 0;
    if (hasWorkers)
    {
        if (opCount >= 1024)
        {
            runPairQueueParallel = true;
            useUniformChunks = true;
        }
        else
        {
            for (auto *collider : pendingColliderOperations)
            {
                if (collider->desiredInGrid)
                {
                    totalAdjacentPairs += collider->adjacentPairs.size();
                }
            }
            runPairQueueParallel = totalAdjacentPairs >= GRID_PAIR_QUEUE_PARALLEL_THRESHOLD;
        }
    }

    if (runPairQueueParallel)
    {
        queueNarrowPhasePairsParallel(useUniformChunks, totalAdjacentPairs);
    }
    else
    {
        queueNarrowPhasePairsSerial();
    }
    pendingColliderOperations.clear();
}

void Grid::clearAll()
{
    for (Collider *collider : pendingColliderOperations)
    {
        collider->setIsRegisteredInGrid(false);
        collider->invalidateFatBounds();
        collider->desiredInGrid = false;
        collider->dirty = 0;
        collider->gridQueueIndex = Collider::INVALID_INDEX;
        collider->cachedCellCount = 0;
    }
    for (auto &cellEntry : cells)
    {
        cellEntry.second.getAABB()->forEachRegisteredCollider([](Collider *collider) {
            collider->setIsRegisteredInGrid(false);
            collider->invalidateFatBounds();
            collider->desiredInGrid = false;
            collider->dirty = 0;
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
    scratch.clear();
    for (auto &local : activePairDeltaCollector.localPairDeltas)
    {
        local.clear();
    }
    activeCellBounds = GridCellRange{};
    activeCellBoundsDirty = false;
    cellSize = GRID_CELL_SIZE;
    resizeCooldown = 0;
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
    if (collider->gridQueueIndex != Collider::INVALID_INDEX)
        return false;
    collider->gridQueueIndex = static_cast<uint32_t>(pendingColliderOperations.size());
    pendingColliderOperations.push_back(collider);
    return true;
}
