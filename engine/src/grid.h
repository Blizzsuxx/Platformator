#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "gridcell.h"
#include "oneapi/tbb.h"

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
    friend class PhysicsManager;

  public:
    using CellMap = std::unordered_map<GridCellKey, GridCell, GridCellKey::Hash>;

    explicit Grid(PhysicsManager *owner = nullptr);
    ~Grid();

    const CellMap &getCells() const;
    bool containsCellPair(Collider *colliderA, Collider *colliderB) const;
    size_t getCandidatePairCount() const;

    void clearPendingNarrowPhasePairs();
    const std::vector<const ColliderPair *> *getPendingNarrowPhasePairs() const;

    void queueSyncCollider(Collider *collider);
    void queueRemoveCollider(Collider *collider);
    void queueAddCollider(Collider *collider);

    void syncCollider(Collider *collider);
    void flushDeferredPairDeltas();
    void flushPendingCellUpdates();
    void clearAll();

    const GridCellRange &getActiveCellBounds() const noexcept
    {
        if (activeCellBoundsDirty)
        {
            recomputeActiveCellBounds();
        }
        return activeCellBounds;
    }

  private:
    void flushPendingWork();
    static bool rangeFitsCellCache(const GridCellRange &range) noexcept;
    void expandActiveCellBounds(const GridCellRange &range) noexcept;
    void recomputeActiveCellBounds() const noexcept;
    void addColliderInternal(Collider *collider, bool geometryPrepared = false);
    void syncColliderInternal(Collider *collider, bool geometryPrepared);
    void removeColliderInternal(Collider *collider);
    bool removeGridCellIfEmpty(GridCell *cell);
    void dequeuePairFromNarrowPhase(const ColliderPair *pair);
    void queuePairForNarrowPhase(const ColliderPair *pair);
    void queuePairsForCollider(Collider *collider);
    const std::vector<const ColliderPair *> *getTouchingPairs(Collider *collider) const;
    GridCell *findCell(const GridCellKey &key);
    const GridCell *findCell(const GridCellKey &key) const;
    GridCell &getOrCreateCell(const GridCellKey &key);
    void queueForEmptyCheck(GridCell *cell);
    void queueForOperations(GridCell *cell);
    void recordPairDelta(Collider *colliderA, Collider *colliderB, int delta);
    void applyCollectedPairDeltas();
    void applyPairWitnessDelta(Collider *colliderA, Collider *colliderB, int delta);
    bool queueCollider(Collider *collider);
    PhysicsManager *owner;
    std::vector<PairDelta> pairDeltaScratch;

    CellMap cells;
    std::vector<CellMap::node_type> freeCellNodes;
    std::unordered_set<ColliderPair, ColliderPair::HashFunction> candidateCollisions;
    std::vector<const ColliderPair *> pendingNarrowPhasePairs;
    std::vector<GridCell *> potentiallyEmptyCells;
    std::vector<GridCell *> cellsWithOperations;
    std::vector<Collider *> pendingColliderOperations;
    PairDeltaCollector activePairDeltaCollector;
    mutable GridCellRange activeCellBounds{};
    mutable bool activeCellBoundsDirty = false;
};
