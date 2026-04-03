#include "grid.h"
#include "constants.h"
#include <cstdio>

Grid::Grid()
    : cells(), candidateCollisions(), pendingNarrowPhasePairs(), pairAdjacency()
{
}

Grid::~Grid()
{
}

void Grid::removeGridCellIfEmpty(GridCell *cell)
{
    if (cell == nullptr || !cell->getAABB()->getIsEmpty())
    {
        return;
    }

    cells.erase(cell->getCellKey());
}

void Grid::addColliderInternal(Collider *collider)
{
    const GridCellRange &cachedRange = collider->getGridCellRange();
    int minX = cachedRange.minX;
    int maxX = cachedRange.maxX;
    int minY = cachedRange.minY;
    int maxY = cachedRange.maxY;

    if constexpr (ENABLE_LOGGING)
    {
        printf("Adding collider %s to grid cells in range (%d, %d) to (%d, %d)\n", collider->getGameObject()->getName().c_str(), minX, minY, maxX, maxY);
    }

    for (int x = minX; x <= maxX; ++x)
    {
        for (int y = minY; y <= maxY; ++y)
        {
            GridCellKey key(x, y);
            auto iterator = cells.find(key);
            if (iterator == cells.end())
            {
                auto [newIterator, inserted] = cells.try_emplace(key, key, this);
                iterator = newIterator;
            }
            iterator->second.addCollider(collider);
        }
    }
}

void Grid::removeColliderInternal(Collider *collider)
{
    const GridCellRange &range = collider->getGridCellRange();
    int minX = range.minX;
    int maxX = range.maxX;
    int minY = range.minY;
    int maxY = range.maxY;

    for (int x = minX; x <= maxX; ++x)
    {
        for (int y = minY; y <= maxY; ++y)
        {
            GridCellKey key(x, y);
            auto iterator = cells.find(key);
            if (iterator != cells.end())
            {
                iterator->second.removeCollider(collider);
                removeGridCellIfEmpty(&iterator->second);
            }
        }
    }
}

void Grid::addCollider(Collider *collider)
{
    addColliderInternal(collider);
}

void Grid::removeCollider(Collider *collider)
{
    removeColliderInternal(collider);
}

void Grid::removePair(const ColliderPair &pair)
{
    auto iterator = candidateCollisions.find(pair);
    if (iterator == candidateCollisions.end())
    {
        return;
    }

    const ColliderPair *storedPair = &(*iterator);

    storedPair->decrementWitnessCount();
    if (storedPair->getWitnessCount() == 0)
    {
        Collider *objectA = storedPair->getObjectA();
        Collider *objectB = storedPair->getObjectB();

        dequeuePairFromNarrowPhase(storedPair);
        storedPair->clearCollision();

        auto adjacencyA = pairAdjacency.find(objectA);
        if (adjacencyA != pairAdjacency.end())
        {
            std::vector<const ColliderPair *> &pairsA = adjacencyA->second;
            size_t removeIndexA = storedPair->getAdjacencyIndexA();
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
            size_t removeIndexB = storedPair->getAdjacencyIndexB();
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
        candidateCollisions.erase(iterator);
    }
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

void Grid::createCollisionPair(Collider *colliderA, Collider *colliderB)
{
    const ColliderPair *pair;

    auto iteratorPair = candidateCollisions.find(ColliderPair(colliderA, colliderB));
    if (iteratorPair == candidateCollisions.end())
    {
        auto [it, in] = candidateCollisions.emplace(colliderA, colliderB);
        pair = &(*it);
    }
    else
    {
        pair = &(*iteratorPair);
    }

    pair->incrementWitnessCount();
    if (pair->getWitnessCount() == 1)
    {

        Collider *objectA = pair->getObjectA();
        Collider *objectB = pair->getObjectB();

        std::vector<const ColliderPair *> &adjacencyA = pairAdjacency[objectA];
        pair->setAdjacencyIndexA(adjacencyA.size());
        adjacencyA.push_back(pair);

        std::vector<const ColliderPair *> &adjacencyB = pairAdjacency[objectB];
        pair->setAdjacencyIndexB(adjacencyB.size());
        adjacencyB.push_back(pair);

        queuePairForNarrowPhase(pair);
    }
}

void Grid::removeCollisionPair(Collider *colliderA, Collider *colliderB)
{
    ColliderPair pair(colliderA, colliderB);
    removePair(pair);
}

const std::unordered_map<GridCellKey, GridCell, GridCellKey::Hash> &Grid::getCells() const
{
    return cells;
}

void Grid::syncCollider(Collider *collider)
{
    if (collider == nullptr || !collider->getGridCellRange().getIsValid())
    {
        return;
    }

    const GridCellRange &oldGridCellRange = collider->getGridCellRange();

    int minX = oldGridCellRange.minX;
    int maxX = oldGridCellRange.maxX;
    int minY = oldGridCellRange.minY;
    int maxY = oldGridCellRange.maxY;

    collider->applyPendingSync();

    const GridCellRange &newGridCellRange = collider->getGridCellRange();

    if constexpr (ENABLE_LOGGING)
    {
        printf("Syncing collider %s with grid cells in range (%d, %d) to (%d, %d)\n", collider->getGameObject()->getName().c_str(), minX, minY, maxX, maxY);
    }

    for (size_t i = 0; i < gridCellsColliderBelongsTo.size();)
    {
        GridCell *aabb = gridCellsColliderBelongsTo[i];
        const GridCellKey &key = aabb->getCellKey();

        if (key.x < minX || key.x > maxX || key.y < minY || key.y > maxY)
        {
            auto iterator = cells.find(key);
            if (iterator != cells.end())
            {
                iterator->second.removeCollider(collider);
                removeGridCellIfEmpty(&iterator->second);
            }
            gridCellsColliderBelongsTo[i] = gridCellsColliderBelongsTo.back();
            gridCellsColliderBelongsTo.pop_back();
            continue;
        }

        ++i;
    }

    collider->applyPendingSync();

    if (projectionsChanged)
    {
        collider->repairRegisteredProjections();
    }

    for (int x = minX; x <= maxX; ++x)
    {
        for (int y = minY; y <= maxY; ++y)
        {
            GridCellKey key(x, y);
            auto cellIterator = std::find_if(gridCellsColliderBelongsTo.begin(), gridCellsColliderBelongsTo.end(), [&](GridCell *gridCell)
                                             { return gridCell->getCellKey() == key; });
            if (cellIterator == gridCellsColliderBelongsTo.end())
            {
                auto [newIterator, inserted] = cells.try_emplace(key, key, this);
                addColliderToGridCell(&newIterator->second, collider);
            }
        }
    }

    collider->setCachedGridCellRange(minX, maxX, minY, maxY);

    queuePairsForCollider(collider);
}