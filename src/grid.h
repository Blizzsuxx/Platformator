#pragma once

#include <unordered_set>
#include <vector>
#include "gridcell.h"
#include "oneapi/tbb.h"

class Grid
{
public:
    Grid();
    ~Grid();

    void addCollider(Collider *collider);
    void removeCollider(Collider *collider);
    void createCollisionPair(Collider *colliderA, Collider *colliderB);
    void removeCollisionPair(Collider *colliderA, Collider *colliderB);
    const tbb::concurrent_unordered_map<GridCellKey, GridCell, GridCellKey::Hash> &getCells() const;
    size_t getCandidatePairCount() const;

    void clearPendingNarrowPhasePairs();
    const std::vector<const ColliderPair *> *getPendingNarrowPhasePairs() const;
    void syncCollider(Collider *collider);
    void flushPendingCellUpdates();

private:
    void addColliderInternal(Collider *collider);
    void removeColliderInternal(Collider *collider);
    void removeGridCellIfEmpty(GridCell *cell);
    void removePair(const ColliderPair &pair);
    void dequeuePairFromNarrowPhase(const ColliderPair *pair);
    void queuePairForNarrowPhase(const ColliderPair *pair);
    void queuePairsForCollider(Collider *collider);
    const std::vector<const ColliderPair *> *getTouchingPairs(Collider *collider) const;
    void queueForEmptyCheck(GridCell *cell);
    void queueForOperations(GridCell *cell);

    tbb::concurrent_unordered_map<GridCellKey, GridCell, GridCellKey::Hash>
        cells;
    std::unordered_set<ColliderPair, ColliderPair::HashFunction> candidateCollisions;
    std::vector<const ColliderPair *> pendingNarrowPhasePairs;
    std::unordered_map<Collider *, std::vector<const ColliderPair *>> pairAdjacency;
    tbb::concurrent_vector<GridCell *> potentiallyEmptyCells;
    tbb::concurrent_vector<GridCell *> cellsWithOperations;
};