#pragma once

#include <oneapi/tbb/enumerable_thread_specific.h>
#include <utility>
#include <vector>

#include "physics/broadphase/gridcell.h"
#include "physics/broadphase/gridmaps.h"

struct PairDelta
{
    uint64_t sortKey;
    Collider *colliderA;
    Collider *colliderB;
    int delta;
};

struct PairDeltaCollector
{
    tbb::enumerable_thread_specific<std::vector<PairDelta>> localPairDeltas;

    static void recordTo(std::vector<PairDelta> &sink, Collider *colliderA, Collider *colliderB, int delta)
    {
        uint32_t slotA = colliderA->getHandle().slot;
        uint32_t slotB = colliderB->getHandle().slot;
        if (slotB < slotA || (slotB == slotA && colliderB < colliderA))
        {
            std::swap(colliderA, colliderB);
            std::swap(slotA, slotB);
        }
        const uint64_t sortKey = (static_cast<uint64_t>(slotA) << 32) | static_cast<uint64_t>(slotB);
        sink.push_back(PairDelta{sortKey, colliderA, colliderB, delta});
    }

    void record(Collider *colliderA, Collider *colliderB, int delta)
    {
        recordTo(localPairDeltas.local(), colliderA, colliderB, delta);
    }
};

class Grid
{
    friend class AABB;
    friend class PhysicsManager;

  public:
    using CellMap = FlatCellMap;

    explicit Grid(PhysicsManager *owner = nullptr);
    ~Grid() = default;

    const CellMap &getCells() const;
    float getCellSize() const noexcept
    {
        return cellSize;
    }
    bool containsCellPair(Collider *colliderA, Collider *colliderB) const;
    size_t getCandidatePairCount() const;

    void clearPendingNarrowPhasePairs();
    const std::vector<ColliderPair *> *getPendingNarrowPhasePairs() const;

    void queueSyncCollider(Collider *collider);
    void queueRemoveCollider(Collider *collider);
    void queueAddCollider(Collider *collider);

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
    struct FlushScratch
    {
        std::vector<PairDelta> pairDeltas;
        std::vector<Collider *> responseDirtyColliders;
        std::vector<std::vector<Collider *>> structuralWorkChunks;
        std::vector<std::vector<ColliderPair *>> pairQueueChunks;
        std::vector<std::pair<size_t, size_t>> pairQueueRanges;
        std::vector<size_t> pairQueueOffsets;

        void clear() noexcept
        {
            pairDeltas.clear();
            responseDirtyColliders.clear();
            for (auto &chunk : structuralWorkChunks)
                chunk.clear();
            for (auto &chunk : pairQueueChunks)
                chunk.clear();
            pairQueueRanges.clear();
            pairQueueOffsets.clear();
        }
    };

    // Deferred flush pipeline stages
    void flushPendingWork();
    size_t preparePendingColliders(bool hasWorkers);
    void applyStructuralColliderOps(size_t prepChunkCount);
    void reclaimEmptyCellsAndMaybeResize();
    void queueNarrowPhasePairsSerial();
    void queueNarrowPhasePairsParallel(bool useUniformChunks, size_t totalAdjacentPairs);
    void flushDirtyCells(size_t operationWeight);

    // Cell sizing & bounds
    static float selectQuantizedCellSize(const std::vector<Collider *> &colliders);
    void rebuildGridForCellSize(float newCellSize);
    static bool rangeFitsCellCache(const GridCellRange &range) noexcept;
    void expandActiveCellBounds(const GridCellRange &range) noexcept;
    void recomputeActiveCellBounds() const noexcept;

    // Collider cell membership
    void addColliderInternal(Collider *collider);
    void syncColliderInternal(Collider *collider);
    void removeColliderInternal(Collider *collider);
    bool removeGridCellIfEmpty(GridCell *cell);

    // Pair tracking & narrow-phase queueing
    void dequeuePairFromNarrowPhase(ColliderPair *pair);
    void queuePairForNarrowPhase(ColliderPair *pair);
    void queuePairsForCollider(Collider *collider);
    const std::vector<ColliderPair *> *getTouchingPairs(Collider *collider) const;
    void recordPairDelta(Collider *colliderA, Collider *colliderB, int delta);
    void applyCollectedPairDeltas();
    void applyPairWitnessDelta(Collider *colliderA, Collider *colliderB, int delta);

    // Cell lookup & internal queues
    GridCell *findCell(const GridCellKey &key);
    const GridCell *findCell(const GridCellKey &key) const;
    GridCell &getOrCreateCell(const GridCellKey &key);
    void queueForEmptyCheck(GridCell *cell);
    void queueForOperations(GridCell *cell);
    bool queueCollider(Collider *collider);
    std::vector<PairDelta> &getLocalPairDeltaSink()
    {
        return activePairDeltaCollector.localPairDeltas.local();
    }

    PhysicsManager *owner;
    CellMap cells;
    FlatCandidatePairPool candidateCollisions;
    std::vector<ColliderPair *> pendingNarrowPhasePairs;
    std::vector<GridCell *> potentiallyEmptyCells;
    std::vector<GridCell *> cellsWithOperations;
    std::vector<Collider *> pendingColliderOperations;
    FlushScratch scratch;
    PairDeltaCollector activePairDeltaCollector;
    float cellSize;
    uint32_t resizeCooldown = 0;
    mutable GridCellRange activeCellBounds{};
    mutable bool activeCellBoundsDirty = false;
};
