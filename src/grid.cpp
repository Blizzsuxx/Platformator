#include "grid.h"
#include "benchmark.h"
#include <algorithm>
#include "constants.h"
#include <cstdio>

Grid::Grid()
    : cells(), candidateCollisions(), pendingNarrowPhasePairs(), pairAdjacency(), potentiallyEmptyCells(), cellsWithOperations(), pendingColliderOperations(), activePairDeltaCollector()
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
                            {
                                GridCell &cell = getOrCreateCell(key);
                                cell.queueAddCollider(collider);
                                queueForOperations(&cell); });

    collider->clearQueuedForAdd();
    collider->setIsRegisteredInGrid(true);
}

void Grid::removeColliderInternal(Collider *collider)
{
    const GridCellRange &range = collider->getGridCellRange();

    PLATFORMATOR_LOG("Removing collider %s from grid cells in range (%d, %d) to (%d, %d)\n", collider->getGameObject()->getName().c_str(), range.minX, range.minY, range.maxX, range.maxY);

    range.forEachCell([&](const GridCellKey &key)
                      {
        if (GridCell *cell = findCell(key))
        {
            cell->queueRemoveBinding(cell->getAABB()->getBinding(collider));
            queueForEmptyCheck(cell);
            queueForOperations(cell);
        } });

    collider->clearQueuedForRemove();
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
    // tbb::parallel_for(size_t(0), pendingNarrowPhasePairs.size(), [&](size_t i)
    //                   {
    //     const ColliderPair *pair = pendingNarrowPhasePairs[i];
    //     if (pair == nullptr)
    //     {
    //         return;
    //     }

    //     pair->setIsQueuedForNarrowPhase(false);
    //     pair->setNarrowPhaseQueueIndex(SIZE_MAX); });

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

        candidateCollisions.erase(candidateCollisions.find(ColliderPair(colliderA, colliderB)));
    }
}

void Grid::applyCollectedPairDeltas()
{
    size_t totalDeltaCount = 0;
    for (const std::vector<PairDelta> &localPairDeltas : activePairDeltaCollector.localPairDeltas)
    {
        totalDeltaCount += localPairDeltas.size();
    }

    if (totalDeltaCount == 0)
    {
        return;
    }

    std::unordered_map<ColliderPair, int, ColliderPair::HashFunction> pairDeltaTotals;

    for (const std::vector<PairDelta> &localPairDeltas : activePairDeltaCollector.localPairDeltas)
    {
        for (const PairDelta &pairDelta : localPairDeltas)
        {
            pairDeltaTotals[ColliderPair(pairDelta.colliderA, pairDelta.colliderB)] += pairDelta.delta;
        }
    }

    for (const auto &[pair, delta] : pairDeltaTotals)
    {
        applyPairWitnessDelta(pair.getObjectA(), pair.getObjectB(), delta);
    }

    for (std::vector<PairDelta> &localPairDeltas : activePairDeltaCollector.localPairDeltas)
    {
        localPairDeltas.clear();
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

void Grid::flushDeferredPairDeltas()
{
    applyCollectedPairDeltas();
}

void Grid::syncCollider(Collider *collider)
{
    if (!collider->getIsQueuedForSync() || collider->getGameObject()->getIsMarkedForDeletion() || !collider->getGameObject()->getActive())
    {
        return;
    }

    collider->prepareSync();

    const GridCellRange oldGridCellRange = collider->getGridCellRange();
    const GridCellRange newGridCellRange = collider->getGridCellRangeCache();
    collider->updateGridCellRange();

    oldGridCellRange.forEachDifference(newGridCellRange, [&](const GridCellKey &key)
                                       {
        if (GridCell *cell = findCell(key))
        {
            cell->queueRemoveBinding(cell->getAABB()->getBinding(collider));
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
            cell->queueSyncBinding(cell->getAABB()->getBinding(collider));
            queueForOperations(cell);
        } });

    collider->removeSync();
}

void Grid::finishColliderSync(Collider *collider)
{
    if (collider == nullptr || collider->getGameObject()->getIsMarkedForDeletion() || !collider->getGameObject()->getActive())
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
    tbb::parallel_for(size_t(0), pendingColliderOperations.size(), [&](size_t i)
                      {
        const ColliderOperation &operation = pendingColliderOperations[i];
        switch (operation.type)
        {
        case ColliderOperation::OperationType::ADD:
            if (!operation.collider->getIsQueuedForRemove())
            {
                addColliderInternal(operation.collider);
            }
            break;
        case ColliderOperation::OperationType::REMOVE:
            removeColliderInternal(operation.collider);
            break;
        case ColliderOperation::OperationType::SYNC:
            if (!operation.collider->getIsQueuedForRemove())
            {
                syncCollider(operation.collider);
            }
            break;
        } });

    tbb::parallel_for(size_t(0), cellsWithOperations.size(), [&](size_t i)
                      {
        GridCell *cell = cellsWithOperations[i];
        cell->flushPendingOperations(); });

    cellsWithOperations.clear();

    applyCollectedPairDeltas();

    for (GridCell *cell : potentiallyEmptyCells)
    {
        cell->clearQueuedForEmpty();
        removeGridCellIfEmpty(cell);
    }

    potentiallyEmptyCells.clear();

    for (const ColliderOperation &operation : pendingColliderOperations)
    {
        if (operation.type == ColliderOperation::OperationType::SYNC)
        {
            finishColliderSync(operation.collider);
        }
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
    if (collider->getIsQueuedForAnything() || collider->getGameObject()->getIsMarkedForDeletion() || !collider->getGameObject()->getActive())
    {
        return;
    }

    collider->markQueuedForAdd();
    pendingColliderOperations.push_back(ColliderOperation{ColliderOperation::OperationType::ADD, collider});
    PLATFORMATOR_BENCH_ADD_COUNTER(QueuedAddCount, 1);
}

void Grid::queueRemoveCollider(Collider *collider)
{
    if (collider->getIsQueuedForRemove())
    {
        return;
    }

    collider->markQueuedForRemove();
    pendingColliderOperations.push_back(ColliderOperation{ColliderOperation::OperationType::REMOVE, collider});
    PLATFORMATOR_BENCH_ADD_COUNTER(QueuedRemoveCount, 1);
}

void Grid::queueSyncCollider(Collider *collider)
{
    if (
        collider->getIsQueuedForAnything() || collider->getGameObject()->getIsMarkedForDeletion() || !collider->getGameObject()->getActive())
    {
        return;
    }

    pendingColliderOperations.push_back(ColliderOperation{ColliderOperation::OperationType::SYNC, collider});
    PLATFORMATOR_BENCH_ADD_COUNTER(QueuedSyncCount, 1);
}