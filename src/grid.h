#pragma once

#include <memory>
#include <unordered_map>
#include <vector>
#include "gridcell.h"
#include "physicspairstate.h"
#include "spatialindex.h"
#include "oneapi/tbb.h"

class Grid
{
    friend class AABB;
    friend class PhysicsManager;

  public:
    explicit Grid(PhysicsManager *owner = nullptr, PhysicsPairState *pairState = nullptr);
    ~Grid();

    bool usesPreparedBounds() const { return preparedBoundsMode; }
    void addCollider(Collider *collider);
    void removeCollider(Collider *collider);
    const tbb::concurrent_unordered_map<GridCellKey, GridCell, GridCellKey::Hash> &getCells() const;
    bool containsCellPair(Collider *colliderA, Collider *colliderB) const;
    size_t getCandidatePairCount() const;
    size_t getOwnedCapacityBytes() const;

    void clearPendingNarrowPhasePairs();
    const std::vector<const ColliderPair *> *getPendingNarrowPhasePairs() const;

    void queueSyncCollider(Collider *collider);
    void queueRemoveCollider(Collider *collider);
    void queueAddCollider(Collider *collider);

    void syncCollider(Collider *collider);
    void flushDeferredPairDeltas();
    void flushPendingCellUpdates();
    void queuePreparedBounds(Collider *, const platformator_detail::SpatialBounds &, uint64_t group, uint64_t mask, bool active);
    platformator_detail::SpatialReplayIndexStats flushPreparedBounds();

  private:
    bool eligiblePair(Collider *, Collider *) const;
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
    void queueCollider(Collider *collider);
    bool preparedBoundsMode = false;
    platformator_detail::SpatialReplayIndexStats replayStats{};
    PhysicsManager *owner;
    std::unique_ptr<PhysicsPairState> localPairState;
    PhysicsPairState *pairStateValue;

    tbb::concurrent_unordered_map<GridCellKey, GridCell, GridCellKey::Hash> cells;
    tbb::concurrent_vector<GridCell *> potentiallyEmptyCells;
    tbb::concurrent_vector<GridCell *> cellsWithOperations;
    std::vector<Collider *> pendingColliderOperations;
};
