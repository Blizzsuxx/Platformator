#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

namespace platformator_test
{
// Main-thread reductions only. Instrumented test builds opt in explicitly.
struct WorkCounters
{
    size_t hierarchyRebuilds = 0;
    size_t transformVisits = 0;
    size_t reconciliations = 0;
    size_t geometryPairs = 0;
    size_t contactTransitionChecks = 0;
    size_t contactEventRecords = 0;
    size_t solverRefreshPairs = 0;
    size_t forceBodies = 0;
    size_t movingBodies = 0;
    size_t sleepBodies = 0;
    size_t gridCellOperations = 0;
    size_t bvhQualityScans = 0;
    size_t pairAdjacencyQueueVisits = 0;
    size_t bvhTreeAdds = 0;
    size_t bvhTreeRemoves = 0;
    size_t bvhTreeMoves = 0;
    size_t bvhMaintenancePasses = 0;
    size_t bvhQueryEndpoints = 0;
    size_t bvhPairChecks = 0;
    size_t bvhContainedMoves = 0;
    size_t bvhRetiredRecords = 0;
    size_t renderSpriteSubmissions = 0;
    size_t renderIndexAdds = 0;
    size_t renderIndexRemoves = 0;
    size_t renderIndexMoves = 0;
    size_t renderMaintenancePasses = 0;
    size_t renderVisibilityQueries = 0;
    size_t renderBoundsPrepared = 0;
    size_t renderResultCacheHits = 0;
    size_t renderFullOrderWalks = 0;
    size_t renderCandidateSortCalls = 0;
    size_t renderSnapshotVisits = 0;
};
inline WorkCounters work;
inline std::vector<size_t> renderSubmittedWindowIndices;
inline std::atomic<size_t> activeCellOperations{0};
inline std::atomic<size_t> maxConcurrentCellOperations{0};
inline std::atomic<size_t> activeSpatialQueries{0};
inline std::atomic<size_t> maxConcurrentSpatialQueries{0};

inline void resetCellConcurrency()
{
    activeCellOperations.store(0, std::memory_order_relaxed);
    maxConcurrentCellOperations.store(0, std::memory_order_relaxed);
}

class CellOperationScope
{
  public:
    CellOperationScope()
    {
        const size_t active = activeCellOperations.fetch_add(1, std::memory_order_relaxed) + 1;
        size_t maximum = maxConcurrentCellOperations.load(std::memory_order_relaxed);
        while (active > maximum &&
               !maxConcurrentCellOperations.compare_exchange_weak(maximum, active, std::memory_order_relaxed))
        {
        }
    }
    ~CellOperationScope() { activeCellOperations.fetch_sub(1, std::memory_order_relaxed); }
};

inline void resetSpatialQueryConcurrency()
{
    activeSpatialQueries.store(0, std::memory_order_relaxed);
    maxConcurrentSpatialQueries.store(0, std::memory_order_relaxed);
}

class SpatialQueryScope
{
  public:
    SpatialQueryScope()
    {
        const size_t active = activeSpatialQueries.fetch_add(1, std::memory_order_relaxed) + 1;
        size_t maximum = maxConcurrentSpatialQueries.load(std::memory_order_relaxed);
        while (active > maximum &&
               !maxConcurrentSpatialQueries.compare_exchange_weak(maximum, active, std::memory_order_relaxed))
        {
        }
    }
    ~SpatialQueryScope() { activeSpatialQueries.fetch_sub(1, std::memory_order_relaxed); }
};
} // namespace platformator_test

#if PLATFORMATOR_ENABLE_WORK_PROBES
#define PLATFORMATOR_WORK_ADD(field, count) (::platformator_test::work.field += (count))
#else
#define PLATFORMATOR_WORK_ADD(field, count) ((void)0)
#define PLATFORMATOR_BVH_QUERY_SCOPE(name) ((void)0)
#endif
