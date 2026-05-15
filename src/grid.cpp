#include "grid.h"
#include <algorithm>
#include "constants.h"
#include <cstdio>

Grid::Grid()
    : cells(), candidateCollisions(), pendingNarrowPhasePairs(), pairAdjacency(), potentiallyEmptyCells(), cellsWithOperations(), pendingPairDeltas(), deferPairDeltas(false)
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
    auto [iterator, inserted] = cells.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(key),
        std::forward_as_tuple(key, this));
    return iterator->second;
}

void Grid::addColliderInternal(Collider *collider)
{
    collider->prepareSync();
    collider->updateGridCellRange();
    const GridCellRange &cachedRange = collider->getGridCellRange();

    PLATFORMATOR_LOG("Adding collider %s to grid cells in range (%d, %d) to (%d, %d)\n", collider->getGameObject()->getName().c_str(), cachedRange.minX, cachedRange.minY, cachedRange.maxX, cachedRange.maxY);

    cachedRange.forEachCell([&](const GridCellKey &key)
                            { getOrCreateCell(key).addCollider(collider); });

    applyPendingPairDeltas();
}

void Grid::removeColliderInternal(Collider *collider)
{
    const GridCellRange &range = collider->getGridCellRange();

    PLATFORMATOR_LOG("Removing collider %s from grid cells in range (%d, %d) to (%d, %d)\n", collider->getGameObject()->getName().c_str(), range.minX, range.minY, range.maxX, range.maxY);

    range.forEachCell([&](const GridCellKey &key)
                      {
        if (GridCell *cell = findCell(key))
        {
            cell->removeCollider(collider);
            removeGridCellIfEmpty(cell);
        } });

    applyPendingPairDeltas();
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
    pair->setIsQueuedForNarrowPhase(false);
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

    pair->setIsQueuedForNarrowPhase(true);
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
    pendingNarrowPhasePairs.clear();
}

void Grid::recordPairDelta(Collider *colliderA, Collider *colliderB, int delta)
{
    if (!deferPairDeltas.load(std::memory_order_acquire))
    {
        applyPairWitnessDelta(colliderA, colliderB, delta);
        return;
    }

    pendingPairDeltas.push_back(PairDelta{colliderA, colliderB, delta});
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
        pair->clearCollision();

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

        candidateCollisions.erase(candidateCollisions.find(ColliderPair(colliderA, colliderB)));
    }
}

void Grid::applyPendingPairDeltas()
{
    if (pendingPairDeltas.empty())
    {
        return;
    }

    std::unordered_map<ColliderPair, int, ColliderPair::HashFunction> pairDeltaTotals;
    pairDeltaTotals.reserve(pendingPairDeltas.size());

    for (const PairDelta &pairDelta : pendingPairDeltas)
    {
        pairDeltaTotals[ColliderPair(pairDelta.colliderA, pairDelta.colliderB)] += pairDelta.delta;
    }

    pendingPairDeltas.clear();

    for (const auto &[pair, delta] : pairDeltaTotals)
    {
        applyPairWitnessDelta(pair.getObjectA(), pair.getObjectB(), delta);
    }
}

const tbb::concurrent_unordered_map<GridCellKey, GridCell, GridCellKey::Hash> &Grid::getCells() const
{
    return cells;
}

size_t Grid::getCandidatePairCount() const
{
    return candidateCollisions.size();
}

void Grid::syncCollider(Collider *collider)
{
    collider->setPendingSyncQueueIndex(SIZE_MAX);
    if (!collider->getIsQueuedForSync())
    {
        return;
    }

    if (collider->getGameObject()->getIsMarkedForDeletion() || !collider->getGameObject()->getActive())
    {
        collider->removeSync();
        return;
    }

    collider->prepareSync();

    const GridCellRange &oldGridCellRange = collider->getGridCellRange();
    const GridCellRange &newGridCellRange = collider->getGridCellRangeCache();

    oldGridCellRange.forEachDifference(newGridCellRange, [&](const GridCellKey &key)
                                       {
        if (GridCell *cell = findCell(key))
        {
            cell->queueRemoveCollider(collider);
            queueForEmptyCheck(cell);
            queueForOperations(cell);
        } });

    newGridCellRange.forEachDifference(oldGridCellRange, [&](const GridCellKey &key)
                                       {
        GridCell &cell = getOrCreateCell(key);
        cell.queueAddCollider(collider);
        queueForOperations(&cell); });

    newGridCellRange.forEachSame(oldGridCellRange, [&](const GridCellKey &key)
                                 {
        if (GridCell *cell = findCell(key))
        {
            cell->queueSyncCollider(collider);
            queueForOperations(cell);
        } });
}

void Grid::finishColliderSync(Collider *collider)
{
    if (collider == nullptr || collider->getGameObject()->getIsMarkedForDeletion() || !collider->getGameObject()->getActive())
    {
        return;
    }

    collider->updateGridCellRange();
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
    deferPairDeltas.store(true, std::memory_order_release);

    tbb::parallel_for(size_t(0), cellsWithOperations.size(), [&](size_t i)
                      {
        GridCell *cell = cellsWithOperations[i];
        cell->flushPendingOperations(); });

    deferPairDeltas.store(false, std::memory_order_release);

    cellsWithOperations.clear();

    applyPendingPairDeltas();

    for (GridCell *cell : potentiallyEmptyCells)
    {
        cell->clearQueuedForEmpty();
        removeGridCellIfEmpty(cell);
    }

    potentiallyEmptyCells.clear();
}

void Grid::queueForOperations(GridCell *cell)
{
    if (!cell->markQueuedForOperations())
    {
        return;
    }

    cellsWithOperations.push_back(cell);
}