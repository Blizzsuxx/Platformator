#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "gridcell.h"
#include "oneapi/tbb.h"

struct ColliderOperation
{
    enum OperationType
    {
        ADD,
        SYNC,
        REMOVE
    };

    OperationType type;
    Collider *collider;
};

struct PairDelta
{
    Collider *colliderA;
    Collider *colliderB;
    int delta;
};

struct PairDeltaCollector
{
    tbb::enumerable_thread_specific<std::vector<PairDelta>> localPairDeltas;

    void record(Collider *colliderA, Collider *colliderB, int delta)
    {
        localPairDeltas.local().emplace_back(colliderA, colliderB, delta);
    }
};

class Grid
{
    friend class AABB;

public:
    Grid();
    ~Grid();

    void addCollider(Collider *collider);
    void removeCollider(Collider *collider);
    const tbb::concurrent_unordered_map<GridCellKey, GridCell, GridCellKey::Hash> &getCells() const;
    size_t getCandidatePairCount() const;

    void clearPendingNarrowPhasePairs();
    const std::vector<const ColliderPair *> *getPendingNarrowPhasePairs() const;

    void queueSyncCollider(Collider *collider);
    void queueRemoveCollider(Collider *collider);
    void queueAddCollider(Collider *collider);

    void syncCollider(Collider *collider);
    void finishColliderSync(Collider *collider);
    void flushDeferredPairDeltas();
    void flushPendingCellUpdates();

private:
    void addColliderInternal(Collider *collider);
    void removeColliderInternal(Collider *collider);
    void removeGridCellIfEmpty(GridCell *cell);
    void dequeuePairFromNarrowPhase(const ColliderPair *pair);
    void queuePairForNarrowPhase(const ColliderPair *pair);
    void queuePairsForCollider(Collider *collider);
    const std::vector<const ColliderPair *> *getTouchingPairs(Collider *collider) const;
    GridCell *findCell(const GridCellKey &key);
    GridCell &getOrCreateCell(const GridCellKey &key);
    void queueForEmptyCheck(GridCell *cell);
    void queueForOperations(GridCell *cell);
    void recordPairDelta(Collider *colliderA, Collider *colliderB, int delta);
    void applyCollectedPairDeltas();
    void applyPairWitnessDelta(Collider *colliderA, Collider *colliderB, int delta);

    tbb::concurrent_unordered_map<GridCellKey, GridCell, GridCellKey::Hash>
        cells;
    std::unordered_set<ColliderPair, ColliderPair::HashFunction> candidateCollisions;
    std::vector<const ColliderPair *> pendingNarrowPhasePairs;
    std::unordered_map<Collider *, std::vector<const ColliderPair *>> pairAdjacency;
    tbb::concurrent_vector<GridCell *> potentiallyEmptyCells;
    tbb::concurrent_vector<GridCell *> cellsWithOperations;
    tbb::concurrent_vector<ColliderOperation> pendingColliderOperations;
    PairDeltaCollector activePairDeltaCollector;
};