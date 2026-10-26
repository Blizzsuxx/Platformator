#include "grid.h"
#include "benchmark.h"
#include <algorithm>
#include "constants.h"
#include <cstdio>
#include "physicsmanager.h"

Grid::Grid(PhysicsManager *owner)
    : owner(owner), cells(), candidateCollisions(), pendingNarrowPhasePairs(), pairAdjacency(), potentiallyEmptyCells(),
      cellsWithOperations(), pendingColliderOperations(), activePairDeltaCollector()
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
    collider->prepareSyncFromCache();
    collider->updateGridCellRange();
    const GridCellRange &cachedRange = collider->getGridCellRange();

    PLATFORMATOR_LOG("Adding collider %s to grid cells in range (%d, %d) to (%d, %d)\n",
                     collider->getGameObject()->getName().c_str(), cachedRange.minX, cachedRange.minY, cachedRange.maxX,
                     cachedRange.maxY);

    cachedRange.forEachCell([&](const GridCellKey &key) {
        GridCell &cell = getOrCreateCell(key);
        cell.queueAddCollider(collider);
        queueForOperations(&cell);
    });

    collider->setIsRegisteredInGrid(true);
}

void Grid::removeColliderInternal(Collider *collider)
{
    const GridCellRange &range = collider->getGridCellRange();

    PLATFORMATOR_LOG("Removing collider %s from grid cells in range (%d, %d) to (%d, %d)\n",
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
    auto iterator = pairAdjacency.find(collider);
    if (iterator == pairAdjacency.end())
    {
        return nullptr;
    }

    return &iterator->second;
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

    auto iteratorPair = candidateCollisions.find(ColliderPair(colliderA, colliderB));
    if (iteratorPair == candidateCollisions.end() && delta < 0)
    {
        return;
    }

    const ColliderPair *pair;
    if (iteratorPair == candidateCollisions.end())
    {
        auto [it, in] = candidateCollisions.emplace(colliderA, colliderB);
        pair = &(*it);
        if (freePairSlots.empty())
        {
            pair->pairSlot = pairSlots.size();
            pairSlots.push_back(pair);
        }
        else
        {
            pair->pairSlot = freePairSlots.back();
            freePairSlots.pop_back();
            pairSlots[pair->pairSlot] = pair;
        }
    }
    else
    {
        pair = &(*iteratorPair);
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

        std::vector<const ColliderPair *> &adjacencyA = pairAdjacency[objectA];
        pair->setAdjacencyIndexA(adjacencyA.size());
        adjacencyA.push_back(pair);

        std::vector<const ColliderPair *> &adjacencyB = pairAdjacency[objectB];
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

        auto adjacencyA = pairAdjacency.find(objectA);
        if (adjacencyA != pairAdjacency.end())
        {
            std::vector<const ColliderPair *> &pairsA = adjacencyA->second;
            size_t removeIndexA = pair->getAdjacencyIndexA();
            size_t lastIndexA = pairsA.size() - 1;
            if (removeIndexA != lastIndexA)
            {
                const ColliderPair *movedPair = pairsA[lastIndexA];
                pairsA[removeIndexA] = movedPair;
                if (movedPair->getObjectA() == objectA)
                {
                    movedPair->setAdjacencyIndexA(removeIndexA);
                }
                else
                {
                    movedPair->setAdjacencyIndexB(removeIndexA);
                }
            }
            pairsA.pop_back();
            if (pairsA.empty())
            {
                pairAdjacency.erase(adjacencyA);
            }
        }

        auto adjacencyB = pairAdjacency.find(objectB);
        if (adjacencyB != pairAdjacency.end())
        {
            std::vector<const ColliderPair *> &pairsB = adjacencyB->second;
            size_t removeIndexB = pair->getAdjacencyIndexB();
            size_t lastIndexB = pairsB.size() - 1;
            if (removeIndexB != lastIndexB)
            {
                const ColliderPair *movedPair = pairsB[lastIndexB];
                pairsB[removeIndexB] = movedPair;
                if (movedPair->getObjectA() == objectB)
                {
                    movedPair->setAdjacencyIndexA(removeIndexB);
                }
                else
                {
                    movedPair->setAdjacencyIndexB(removeIndexB);
                }
            }
            pairsB.pop_back();
            if (pairsB.empty())
            {
                pairAdjacency.erase(adjacencyB);
            }
        }

        if (owner)
            owner->removePair(pair);
        pairSlots[pair->pairSlot] = nullptr;
        freePairSlots.push_back(pair->pairSlot);
        candidateCollisions.erase(candidateCollisions.find(ColliderPair(colliderA, colliderB)));
    }
}

void Grid::applyCollectedPairDeltas()
{
    pairDeltaScratch.clear();
    for (auto &local : activePairDeltaCollector.localPairDeltas)
    {
        for (auto delta : local)
        {
            if (delta.colliderB->getHandle() < delta.colliderA->getHandle())
                std::swap(delta.colliderA, delta.colliderB);
            pairDeltaScratch.push_back(delta);
        }
        local.clear();
    }
    std::sort(pairDeltaScratch.begin(), pairDeltaScratch.end(), [](const auto &a, const auto &b) {
        if (a.colliderA->getHandle() != b.colliderA->getHandle())
            return a.colliderA->getHandle() < b.colliderA->getHandle();
        return a.colliderB->getHandle() < b.colliderB->getHandle();
    });
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
    return candidateCollisions.size();
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
        collider->prepareSyncFromCache();
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

void Grid::finishColliderSync(Collider *collider)
{
    if (collider == nullptr || collider->getGameObject()->getIsMarkedForDeletion() ||
        !collider->getGameObject()->getActive())
    {
        return;
    }

    queuePairsForCollider(collider);
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
    tbb::parallel_for(size_t(0), pendingColliderOperations.size(), [&](size_t i) {
        auto *collider = pendingColliderOperations[i];
        if (!collider->desiredInGrid)
        {
            if (collider->getIsRegisteredInGrid())
                removeColliderInternal(collider);
        }
        else if (!collider->getIsRegisteredInGrid())
            addColliderInternal(collider);
        else if (collider->dirty)
            syncCollider(collider);
    });
    tbb::parallel_for(size_t(0), cellsWithOperations.size(),
                      [&](size_t i) { cellsWithOperations[i]->flushPendingOperations(); });
    cellsWithOperations.clear();
    applyCollectedPairDeltas();
    for (auto *cell : potentiallyEmptyCells)
    {
        cell->clearQueuedForEmpty();
        removeGridCellIfEmpty(cell);
    }
    potentiallyEmptyCells.clear();
    for (auto *collider : pendingColliderOperations)
    {
        if (collider->desiredInGrid)
            queuePairsForCollider(collider);
        if (owner && (collider->dirty & Collider::Response))
            owner->refreshResponse(collider);
        collider->gridQueueIndex = SIZE_MAX;
        collider->dirty = 0;
    }
    pendingColliderOperations.clear();
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
    queueCollider(collider);
    PLATFORMATOR_BENCH_ADD_COUNTER(QueuedSyncCount, 1);
}
void Grid::queueCollider(Collider *collider)
{
    if (collider->gridQueueIndex != SIZE_MAX)
        return;
    collider->gridQueueIndex = pendingColliderOperations.size();
    pendingColliderOperations.push_back(collider);
}
