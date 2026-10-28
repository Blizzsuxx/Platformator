#include "grid.h"
#include "benchmark.h"
#include "spatialresourcehooks.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include "constants.h"
#include <cstdio>
#include "physicsmanager.h"
#include "workprobe.h"
#include <memory>

Grid::Grid(PhysicsManager *owner, PhysicsPairState *pairState)
    : owner(owner), localPairState(pairState ? nullptr : std::make_unique<PhysicsPairState>(owner)),
      pairStateValue(pairState ? pairState : localPairState.get()), cells(), potentiallyEmptyCells(), cellsWithOperations(),
      pendingColliderOperations()
{
}

Grid::~Grid()
{
}

void Grid::removeGridCellIfEmpty(GridCell *cell)
{
    if (!cell->getAABB()->getIsEmpty())
    {
        return;
    }

    cells.unsafe_erase(cell->getCellKey());
}

GridCell *Grid::findCell(const GridCellKey &key)
{
    auto iterator = cells.find(key);
    if (iterator == cells.end())
    {
        return nullptr;
    }

    return &iterator->second;
}

GridCell &Grid::getOrCreateCell(const GridCellKey &key)
{
    auto [iterator, inserted] =
        cells.emplace(std::piecewise_construct, std::forward_as_tuple(key), std::forward_as_tuple(key, this));
    return iterator->second;
}

void Grid::addColliderInternal(Collider *collider)
{
    if (preparedBoundsMode) collider->calculateGridCellRange();
    else collider->prepareSyncFromCache();
    collider->updateGridCellRange();
    const GridCellRange &cachedRange = collider->getGridCellRange();

    if (!preparedBoundsMode) PLATFORMATOR_LOG("Adding collider %s to grid cells in range (%d, %d) to (%d, %d)\n",
                     collider->getGameObject()->getName().c_str(), cachedRange.minX, cachedRange.minY, cachedRange.maxX,
                     cachedRange.maxY);

    cachedRange.forEachCell([&](const GridCellKey &key) {
        GridCell &cell = getOrCreateCell(key);
        cell.queueAddCollider(collider);
        queueForOperations(&cell);
    });

    collider->setIsRegisteredInGrid(true);
    collider->setIsRegisteredInBroadPhase(true);
}

void Grid::removeColliderInternal(Collider *collider)
{
    const GridCellRange &range = collider->getGridCellRange();

    if (!preparedBoundsMode) PLATFORMATOR_LOG("Removing collider %s from grid cells in range (%d, %d) to (%d, %d)\n",
                     collider->getGameObject()->getName().c_str(), range.minX, range.minY, range.maxX, range.maxY);

    range.forEachCell([&](const GridCellKey &key) {
        if (GridCell *cell = findCell(key))
        {
            cell->queueRemoveBinding(cell->getAABB()->getBinding(collider));
            queueForEmptyCheck(cell);
            queueForOperations(cell);
        }
    });

    collider->setIsRegisteredInGrid(false);
    collider->setIsRegisteredInBroadPhase(false);
}

void Grid::addCollider(Collider *collider)
{
    addColliderInternal(collider);
}

void Grid::removeCollider(Collider *collider)
{
    removeColliderInternal(collider);
}

void Grid::dequeuePairFromNarrowPhase(const ColliderPair *pair)
{
    pairStateValue->dequeuePairFromNarrowPhase(pair);
}

void Grid::queuePairsForCollider(Collider *collider)
{
    pairStateValue->queuePairsForCollider(collider);
}

void Grid::queuePairForNarrowPhase(const ColliderPair *pair)
{
    pairStateValue->queuePairForNarrowPhase(pair);
}

const std::vector<const ColliderPair *> *Grid::getPendingNarrowPhasePairs() const
{
    return pairStateValue->getPendingNarrowPhasePairs();
}

const std::vector<const ColliderPair *> *Grid::getTouchingPairs(Collider *collider) const
{
    return pairStateValue->getTouchingPairs(collider);
}

void Grid::clearPendingNarrowPhasePairs()
{
    pairStateValue->clearPendingNarrowPhasePairs();
}

void Grid::recordPairDelta(Collider *colliderA, Collider *colliderB, int delta)
{
    pairStateValue->recordPairDelta(colliderA, colliderB, delta);
}

void Grid::applyCollectedPairDeltas()
{
    const auto stats = pairStateValue->applyCollectedPairDeltas();
    if (preparedBoundsMode)
    {
        replayStats.rawDiscoveries = stats.rawDiscoveries;
        replayStats.exactOverlaps = stats.exactChecks;
        replayStats.pairAdds = stats.pairAdds;
        replayStats.pairRemoves = stats.pairRemoves;
    }
}

const tbb::concurrent_unordered_map<GridCellKey, GridCell, GridCellKey::Hash> &Grid::getCells() const
{
    return cells;
}

bool Grid::containsCellPair(Collider *colliderA, Collider *colliderB) const
{
    const AABBPair pair(colliderA, colliderB);

    for (const auto &cellEntry : cells)
    {
        const AABB *aabb = cellEntry.second.getAABB();
        if (aabb->pairsWithBothAxisOverlapping.find(pair) != aabb->pairsWithBothAxisOverlapping.end())
        {
            return true;
        }
    }

    return false;
}

size_t Grid::getCandidatePairCount() const
{
    return pairStateValue->getCandidatePairCount();
}

size_t Grid::getOwnedCapacityBytes() const
{
    size_t bytes = sizeof(*this) + cells.unsafe_bucket_count() * sizeof(void *) +
                   cells.size() * (sizeof(decltype(cells)::value_type) + 2*sizeof(void *)) +
                   pendingColliderOperations.capacity() * sizeof(Collider *) +
                   potentiallyEmptyCells.capacity() * sizeof(GridCell *) +
                   cellsWithOperations.capacity() * sizeof(GridCell *);
    for (const auto &entry : cells) bytes += entry.second.getOwnedCapacityBytes();
    return bytes;
}

void Grid::flushDeferredPairDeltas()
{
    applyCollectedPairDeltas();
}

void Grid::syncCollider(Collider *collider)
{
    const bool geometry = collider->dirty & Collider::Geometry;
    const bool filter = collider->dirty & Collider::Filter;
    if (geometry)
    {
        if (preparedBoundsMode) collider->calculateGridCellRange();
        else collider->prepareSyncFromCache();
    }
    if (!geometry && !filter)
        return;
    const auto oldRange = collider->getGridCellRange();
    const auto newRange = collider->getGridCellRangeCache();
    collider->updateGridCellRange();
    const auto remove = [&](const GridCellKey &key) {
        auto *cell = findCell(key);
        cell->queueRemoveBinding(cell->getAABB()->getBinding(collider));
        queueForEmptyCheck(cell);
        queueForOperations(cell);
    };
    const auto add = [&](const GridCellKey &key) {
        auto &cell = getOrCreateCell(key);
        cell.queueAddCollider(collider);
        queueForOperations(&cell);
    };
    if (filter)
    {
        oldRange.forEachCell(remove);
        newRange.forEachCell(add);
    }
    else
    {
        oldRange.forEachDifference(newRange, remove);
        newRange.forEachDifference(oldRange, add);
        newRange.forEachSame(oldRange, [&](const GridCellKey &key) {
            auto *cell = findCell(key);
            cell->queueSyncBinding(cell->getAABB()->getBinding(collider));
            queueForOperations(cell);
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
    platformator_detail::SpatialResourceTagScope resourceTag(platformator_detail::SpatialResourceTag::PhysicsIndex);
    PLATFORMATOR_BENCH_SCOPE(SpatialUpdate);
    if (pendingColliderOperations.empty() && cellsWithOperations.empty() && potentiallyEmptyCells.empty())
        return;
    const auto updateStarted = std::chrono::steady_clock::now();
    if (preparedBoundsMode)
        for (const auto *collider : pendingColliderOperations)
        {
            replayStats.proxyInserts += collider->desiredInBroadPhase && !collider->getIsRegisteredInGrid();
            replayStats.proxyRemoves += !collider->desiredInBroadPhase && collider->getIsRegisteredInGrid();
            replayStats.proxyMoves += collider->desiredInBroadPhase && collider->getIsRegisteredInGrid() && (collider->dirty & Collider::Geometry);
        }
    {
        PLATFORMATOR_BENCH_SCOPE(BoundsPrepare);
        for (const auto *collider : pendingColliderOperations)
        {
            if (!collider) continue;
            if ((collider->dirty & Collider::Geometry) != 0)
                PLATFORMATOR_BENCH_ADD_COUNTER(ChangedBoundsCount, 1);
            if ((collider->dirty & Collider::Filter) != 0)
                PLATFORMATOR_BENCH_ADD_COUNTER(FilterChangeCount, 1);
            if (collider->desiredInBroadPhase && !collider->getIsRegisteredInGrid())
                PLATFORMATOR_BENCH_ADD_COUNTER(ProxyInsertCount, 1);
            else if (!collider->desiredInBroadPhase && collider->getIsRegisteredInGrid())
                PLATFORMATOR_BENCH_ADD_COUNTER(ProxyRemoveCount, 1);
            else if ((collider->dirty & Collider::Geometry) != 0)
                PLATFORMATOR_BENCH_ADD_COUNTER(ProxyMoveCount, 1);
        }
        tbb::parallel_for(size_t(0), pendingColliderOperations.size(), [&](size_t i) {
            auto *collider = pendingColliderOperations[i];
            if (!collider->desiredInBroadPhase)
            {
                if (collider->getIsRegisteredInGrid())
                    removeColliderInternal(collider);
            }
            else if (!collider->getIsRegisteredInGrid())
                addColliderInternal(collider);
            else if (collider->dirty)
                syncCollider(collider);
        });
    }
    PLATFORMATOR_BENCH_ADD_COUNTER(QueryCount, cellsWithOperations.size());
    PLATFORMATOR_WORK_ADD(gridCellOperations, cellsWithOperations.size());
    {
        PLATFORMATOR_BENCH_SCOPE(IndexUpdate);
        tbb::parallel_for(size_t(0), cellsWithOperations.size(), [&](size_t i) {
            PLATFORMATOR_WORK_CELL_SCOPE(cellOperationScope);
            cellsWithOperations[i]->flushPendingOperations();
        });
    }
    cellsWithOperations.clear();
    if (preparedBoundsMode) replayStats.updateNanoseconds = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-updateStarted).count());
    const auto reduceStarted = std::chrono::steady_clock::now();
    {
        PLATFORMATOR_BENCH_SCOPE(ResultReduce);
        applyCollectedPairDeltas();
    }
    for (auto *cell : potentiallyEmptyCells)
    {
        cell->clearQueuedForEmpty();
        removeGridCellIfEmpty(cell);
    }
    potentiallyEmptyCells.clear();
    for (auto *collider : pendingColliderOperations)
    {
        if (collider->desiredInBroadPhase)
            queuePairsForCollider(collider);
        if (owner && (collider->dirty & Collider::Response))
            owner->refreshResponse(collider);
        collider->broadPhaseQueueIndex = SIZE_MAX;
        collider->dirty = 0;
    }
    pendingColliderOperations.clear();
    if (preparedBoundsMode) replayStats.reduceNanoseconds = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-reduceStarted).count());
    PLATFORMATOR_BENCH_SET_COUNTER(SpatialProxyCount, owner ? owner->getColliders().size() : 0);
    PLATFORMATOR_BENCH_SET_COUNTER(OccupiedCellCount, cells.size());
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
    collider->desiredInBroadPhase = true;
    collider->dirty |= Collider::Geometry;
    queueCollider(collider);
    PLATFORMATOR_BENCH_ADD_COUNTER(QueuedAddCount, 1);
}

void Grid::queueRemoveCollider(Collider *collider)
{
    collider->desiredInBroadPhase = false;
    queueCollider(collider);
    PLATFORMATOR_BENCH_ADD_COUNTER(QueuedRemoveCount, 1);
}

void Grid::queueSyncCollider(Collider *collider)
{
    queueCollider(collider);
    PLATFORMATOR_BENCH_ADD_COUNTER(QueuedSyncCount, 1);
}
void Grid::queueCollider(Collider *collider)
{
    if (collider->broadPhaseQueueIndex != SIZE_MAX)
        return;
    collider->broadPhaseQueueIndex = pendingColliderOperations.size();
    pendingColliderOperations.push_back(collider);
}

void Grid::queuePreparedBounds(Collider *collider, const platformator_detail::SpatialBounds &bounds,
                              uint64_t group, uint64_t mask, bool active)
{
    if (owner || !collider)
        throw std::invalid_argument("Prepared Grid bounds require isolated replay endpoints");
    if (!bounds.isFiniteOrdered()) throw std::invalid_argument("Prepared Grid bounds must be finite and ordered");
    // Preflight coordinate conversion before touching a queued endpoint.
    const auto cell = [](float value) {
        const double coordinate = std::floor(double(value) / GRID_CELL_SIZE);
        if (coordinate <= std::numeric_limits<int>::min() || coordinate >= std::numeric_limits<int>::max())
            throw std::out_of_range("Prepared Grid coordinate is out of range");
    };
    cell(bounds.minX); cell(bounds.maxX); cell(bounds.minY); cell(bounds.maxY);
    preparedBoundsMode = true;
    auto *x=collider->getXProjections(); auto *y=collider->getYProjections();
    const bool geometry = !collider->getIsRegisteredInGrid() ||
        x->getMin()->getProjectedPosition()!=bounds.minX || x->getMax()->getProjectedPosition()!=bounds.maxX ||
        y->getMin()->getProjectedPosition()!=bounds.minY || y->getMax()->getProjectedPosition()!=bounds.maxY;
    if (geometry) collider->dirty |= Collider::Geometry;
    if (collider->collisionGroup != group || collider->collisionMask != mask) collider->dirty |= Collider::Filter;
    x->getMin()->setProjectedPosition(bounds.minX); x->getMax()->setProjectedPosition(bounds.maxX);
    y->getMin()->setProjectedPosition(bounds.minY); y->getMax()->setProjectedPosition(bounds.maxY);
    collider->collisionGroup=group; collider->collisionMask=mask;
    collider->desiredInBroadPhase=active;
    queueCollider(collider);
}

platformator_detail::SpatialReplayIndexStats Grid::flushPreparedBounds()
{
    replayStats = {};
    flushPendingCellUpdates();
    return replayStats;
}

bool Grid::eligiblePair(Collider *a, Collider *b) const
{
    const bool active = preparedBoundsMode ? a->desiredInBroadPhase && b->desiredInBroadPhase
        : a->getGameObject()->getActive() && b->getGameObject()->getActive();
    return active && (a->getCollisionGroup() & b->getCollisionMask()) != 0 &&
                     (b->getCollisionGroup() & a->getCollisionMask()) != 0;
}
