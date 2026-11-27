#include "physics/broadphase/grid.h"
#include "benchmark/benchmark.h"
#include <algorithm>
#include <array>
#include <cmath>
#include "core/constants.h"
#include "core/membership.h"
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
      cellsWithOperations(), pendingColliderOperations(), activePairDeltaCollector(), cellSize(GRID_CELL_SIZE),
      resizeCooldown(0)
{
}

float Grid::selectQuantizedCellSize(const std::vector<Collider *> &colliders)
{
    constexpr std::array<float, 7> allowedCellSizes{12.5f, 25.0f, 50.0f, 100.0f, 200.0f, 400.0f, 800.0f};
    constexpr size_t maxSamples = 512;

    std::vector<float> dynamicExtents;
    std::vector<float> allExtents;
    const size_t n = colliders.size();
    const size_t stride = std::max<size_t>(1, n / maxSamples);
    dynamicExtents.reserve(std::min(n, maxSamples));
    allExtents.reserve(std::min(n, maxSamples));

    for (size_t i = 0; i < n; i += stride)
    {
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
        allExtents.push_back(extent);
        const auto *rb = c->getAttachedRigidbody();
        if (rb != nullptr && rb->getBodyType() != BodyType::STATIC)
        {
            dynamicExtents.push_back(extent);
        }
    }

    auto &samples = dynamicExtents.size() >= 16 ? dynamicExtents : allExtents;
    if (samples.size() < 16)
    {
        return GRID_CELL_SIZE;
    }

    const size_t mid = samples.size() / 2;
    std::nth_element(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(mid), samples.end());
    const float target = samples[mid] * 7.1f;

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

    if (registered.size() < GRID_GEOMETRY_PREP_PARALLEL_THRESHOLD)
    {
        for (Collider *collider : registered)
        {
            collider->calculateGridCellRange(cellSize);
        }
    }
    else
    {
        tbb::parallel_for(size_t(0), registered.size(),
                          [&](size_t i) { registered[i]->calculateGridCellRange(cellSize); });
    }

    for (Collider *collider : registered)
    {
        addColliderInternal(collider, true);
    }

    if (!cellsWithOperations.empty())
    {
        const bool runCellsParallel =
            cellsWithOperations.size() >= GRID_CELL_FLUSH_PARALLEL_THRESHOLD ||
            (cellsWithOperations.size() >= GRID_CELL_FLUSH_DENSE_MIN_CELLS &&
             registered.size() >= GRID_UPDATE_PARALLEL_THRESHOLD);
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

    applyCollectedPairDeltas();
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
        collider->prepareSyncFromCache(cellSize);
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

void Grid::dequeuePairFromNarrowPhase(const ColliderPair *pair)
{
    if (pair == nullptr || !pair->getIsQueuedForNarrowPhase())
    {
        return;
    }

    swapRemoveIndexed(
        pendingNarrowPhasePairs, pair, [](const ColliderPair *p) { return p->getNarrowPhaseQueueIndex(); },
        [](const ColliderPair *p, size_t idx) { p->setNarrowPhaseQueueIndex(idx); });
}

namespace
{
inline bool pairNeedsNarrowPhaseCheck(const ColliderPair *pair) noexcept
{
    if (pair->getCollision() != nullptr)
    {
        return true;
    }
    const Collider *a = pair->getObjectA();
    const Collider *b = pair->getObjectB();
    return !(a->getTightMaxX() < b->getTightMinX() || b->getTightMaxX() < a->getTightMinX() ||
             a->getTightMaxY() < b->getTightMinY() || b->getTightMaxY() < a->getTightMinY());
}
} // namespace

void Grid::queuePairsForCollider(Collider *collider)
{
    const std::vector<const ColliderPair *> *touchingPairs = getTouchingPairs(collider);
    if (touchingPairs == nullptr)
    {
        return;
    }

    for (const ColliderPair *pair : *touchingPairs)
    {
        if (pair != nullptr && !pair->getIsQueuedForNarrowPhase() && pairNeedsNarrowPhaseCheck(pair))
        {
            queuePairForNarrowPhase(pair);
        }
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
        collider->prepareSyncFromCache(cellSize);
    if (!geometry && !filter)
        return;
    if (!filter && !collider->broadPhaseSyncNeeded)
        return;
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

void Grid::flushPendingWork()
{
    const auto prepareColliderGeometry = [this](Collider *collider) {
        if (collider->desiredInGrid && (!collider->getIsRegisteredInGrid() || (collider->dirty & Collider::Geometry)))
        {
            collider->prepareSyncFromCache(cellSize);
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
    if (cells.empty() && pendingColliderOperations.size() >= 32)
    {
        const float desiredCellSize = selectQuantizedCellSize(pendingColliderOperations);
        if (desiredCellSize != cellSize)
        {
            cellSize = desiredCellSize;
            const auto recomputeRange = [this](Collider *collider) {
                if (collider->desiredInGrid)
                {
                    collider->calculateGridCellRange(cellSize);
                }
            };
            if (pendingColliderOperations.size() < GRID_GEOMETRY_PREP_PARALLEL_THRESHOLD)
            {
                for (auto *collider : pendingColliderOperations)
                    recomputeRange(collider);
            }
            else
            {
                tbb::parallel_for(size_t(0), pendingColliderOperations.size(),
                                  [&](size_t i) { recomputeRange(pendingColliderOperations[i]); });
            }
        }
    }
    responseDirtyColliders.clear();
    for (auto *collider : pendingColliderOperations)
    {
        if (!collider->desiredInGrid)
        {
            if (collider->getIsRegisteredInGrid())
                removeColliderInternal(collider);
        }
        else if (!collider->getIsRegisteredInGrid())
        {
            addColliderInternal(collider, true);
            if (owner && (collider->dirty & Collider::Response))
                responseDirtyColliders.push_back(collider);
        }
        else
        {
            if (collider->broadPhaseSyncNeeded || (collider->dirty & Collider::Filter))
                syncColliderInternal(collider, true);
            if (owner && (collider->dirty & Collider::Response))
                responseDirtyColliders.push_back(collider);
        }
    }
    if (!cellsWithOperations.empty())
    {
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
    }
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
            if (desiredCellSize <= cellSize * 0.5f)
            {
                rebuildGridForCellSize(desiredCellSize);
                resizeCooldown = 120;
            }
        }
    }

    if (owner && !responseDirtyColliders.empty())
    {
        for (auto *collider : responseDirtyColliders)
        {
            owner->refreshResponse(collider);
        }
        responseDirtyColliders.clear();
    }

    const size_t opCount = pendingColliderOperations.size();
    const bool hasWorkers =
        tbb::global_control::active_value(tbb::global_control::max_allowed_parallelism) > 1 &&
        tbb::this_task_arena::max_concurrency() > 1;
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
        pairQueueChunkRanges.clear();
        if (useUniformChunks)
        {
            const size_t chunkCount = std::min<size_t>(32, (opCount + 127) / 128);
            const size_t base = opCount / chunkCount;
            const size_t rem = opCount % chunkCount;
            size_t cursor = 0;
            for (size_t c = 0; c < chunkCount; ++c)
            {
                const size_t len = base + (c < rem ? 1 : 0);
                pairQueueChunkRanges.emplace_back(cursor, cursor + len);
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
                if (!collider->desiredInGrid || collider->adjacentPairs.empty())
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
                    if ((!other->desiredInGrid || other->gridQueueIndex > idx) && pairNeedsNarrowPhaseCheck(pair))
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
        const bool hasWrites = writeOffset > pendingNarrowPhasePairs.size();
        if (hasWrites)
        {
            pendingNarrowPhasePairs.resize(writeOffset);
        }
        // Pass 2: scatter narrow-phase pairs AND reset gridQueueIndex/dirty in parallel (gridQueueIndex no longer read).
        tbb::parallel_for(size_t(0), chunkCount, [&](size_t chunkIdx) {
            if (hasWrites)
            {
                const auto &local = pairQueueChunkScratch[chunkIdx];
                size_t outIdx = pairQueueChunkOffsets[chunkIdx];
                for (const ColliderPair *pair : local)
                {
                    pair->setNarrowPhaseQueueIndex(outIdx);
                    pendingNarrowPhasePairs[outIdx] = pair;
                    ++outIdx;
                }
            }
            const auto [start, end] = pairQueueChunkRanges[chunkIdx];
            for (size_t idx = start; idx < end; ++idx)
            {
                Collider *collider = pendingColliderOperations[idx];
                collider->gridQueueIndex = SIZE_MAX;
                collider->dirty = 0;
            }
        });
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
        collider->gridQueueIndex = SIZE_MAX;
        collider->cachedCellCount = 0;
    }
    for (auto &cellEntry : cells)
    {
        cellEntry.second.getAABB()->forEachRegisteredCollider([](Collider *collider) {
            collider->invalidateFatBounds();
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
    responseDirtyColliders.clear();
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
    if (collider->gridQueueIndex != SIZE_MAX)
        return false;
    collider->gridQueueIndex = pendingColliderOperations.size();
    pendingColliderOperations.push_back(collider);
    return true;
}
