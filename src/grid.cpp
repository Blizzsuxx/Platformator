#include "grid.h"
#include "constants.h"

Grid::Grid()
    : cells(), candidateCollisions(), pendingNarrowPhasePairs(), pairAdjacency()
{
}

Grid::~Grid()
{
}

std::tuple<int, int, int, int> Grid::getGridCellRange(Collider *collider)
{
    BoundingRadiusProjectionAxis *xProjections = collider->getXProjections();
    BoundingRadiusProjectionAxis *yProjections = collider->getYProjections();

    int minX = static_cast<int>(std::floor(xProjections->getMin()->getProjectedPosition() / GRID_CELL_SIZE));
    int maxX = static_cast<int>(std::floor(xProjections->getMax()->getProjectedPosition() / GRID_CELL_SIZE));
    int minY = static_cast<int>(std::floor(yProjections->getMin()->getProjectedPosition() / GRID_CELL_SIZE));
    int maxY = static_cast<int>(std::floor(yProjections->getMax()->getProjectedPosition() / GRID_CELL_SIZE));

    return std::move(std::make_tuple(minX, maxX, minY, maxY));
}

void Grid::addColliderToGridCell(GridCell *cell, Collider *collider)
{
    if (cell != nullptr)
    {
        cell->addCollider(collider);
        collider->addToGridCell(cell);
    }
}

void Grid::addColliderInternal(Collider *collider)
{
    int minX, maxX, minY, maxY;
    std::tie(minX, maxX, minY, maxY) = getGridCellRange(collider);

    for (int x = minX; x <= maxX; ++x)
    {
        for (int y = minY; y <= maxY; ++y)
        {
            GridCellKey key(x, y);
            auto iterator = cells.find(key);
            if (iterator == cells.end())
            {
                auto [newIterator, inserted] = cells.emplace(key, GridCell(key, this));
                iterator = newIterator;
            }
            addColliderToGridCell(&iterator->second, collider);
        }
    }
}

void Grid::removeColliderInternal(Collider *collider)
{
    int minX, maxX, minY, maxY;
    std::tie(minX, maxX, minY, maxY) = getGridCellRange(collider);
    std::vector<GridCell *> &gridCells = collider->getGridCells();

    for (int i = 0; i < gridCells.size(); i++)
    {
        GridCell *cell = gridCells[i];
        cell->removeCollider(collider);
        if (cell->getAABB()->getIsEmpty())
        {
            cells.erase(cell->getCellKey());
        }
    }
    collider->clearGridCells();
}

void Grid::addCollider(Collider *collider)
{
    addColliderInternal(collider);
}

void Grid::removeCollider(Collider *collider)
{
    removeColliderInternal(collider);
}

void Grid::removePair(const ColliderPair &pair, Axis axis)
{
    auto iterator = candidateCollisions.find(pair);
    if (iterator == candidateCollisions.end())
    {
        return;
    }

    const ColliderPair *storedPair = &(*iterator);

    storedPair->decrementWitnessCount(axis);
    if (storedPair->getWitnessCountMin() == 0)
    {
        // if no collisions on any axis, remove the pair

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

void Grid::createCollisionPair(Collider *colliderA, Collider *colliderB, Axis axis)
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

    pair->incrementWitnessCount(axis);
    if (pair->getWitnessCountMax() == 1)
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

void Grid::removeCollisionPair(Collider *colliderA, Collider *colliderB, Axis axis)
{
    ColliderPair pair(colliderA, colliderB);
    removePair(pair, axis);
}

void Grid::queueColliderForUpdate(Collider *collider)
{
    queuePairsForCollider(collider);
    syncColliderWithGrid(collider);
}

void Grid::syncColliderWithGrid(Collider *collider)
{
    std::vector<GridCell *> &gridCellsColliderBelongsTo = collider->getGridCells();

    int minX, maxX, minY, maxY;
    std::tie(minX, maxX, minY, maxY) = getGridCellRange(collider);

    for (int i = 0; i < gridCellsColliderBelongsTo.size(); ++i)
    {
        GridCell *aabb = gridCellsColliderBelongsTo[i];
        const GridCellKey &key = aabb->getCellKey();

        if (key.x < minX || key.x > maxX || key.y < minY || key.y > maxY)
        {
            auto iterator = cells.find(key);
            if (iterator != cells.end())
            {
                iterator->second.removeCollider(collider);
            }
            gridCellsColliderBelongsTo.erase(gridCellsColliderBelongsTo.begin() + i);

            if (iterator != cells.end() && iterator->second.getAABB()->getIsEmpty())
            {
                cells.erase(iterator);
            }
            --i;
        }
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
                auto [newIterator, inserted] = cells.emplace(key, GridCell(key, this));
                addColliderToGridCell(&newIterator->second, collider);
            }
        }
    }
}

void Grid::sort()
{
    // TODO: sort
    for (auto &cellEntry : cells)
    {
        cellEntry.second.getAABB()->sort();
    }
}