#pragma once

#include <vector>
#include "physics/broadphase/gridcell.h"
#include "physics/broadphase/gridmaps.h"
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
    using CellMap = FlatCellMap;

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
    FlatCandidatePairPool candidateCollisions;
    std::vector<const ColliderPair *> pendingNarrowPhasePairs;
    std::vector<GridCell *> potentiallyEmptyCells;
    std::vector<GridCell *> cellsWithOperations;
    std::vector<Collider *> pendingColliderOperations;
    std::vector<std::vector<const ColliderPair *>> pairQueueChunkScratch;
    std::vector<std::pair<size_t, size_t>> pairQueueChunkRanges;
    std::vector<size_t> pairQueueChunkOffsets;
    PairDeltaCollector activePairDeltaCollector;
    mutable GridCellRange activeCellBounds{};
    mutable bool activeCellBoundsDirty = false;
};
