#include "renderculling.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "runtime_work_probe.h"
#include "test_support.h"

namespace
{
thread_local long failAllocationAfter = -1;
thread_local bool countAllocations = false;
thread_local size_t observedAllocations = 0;
}

void *operator new(std::size_t bytes)
{
    if (countAllocations)
        ++observedAllocations;
    if (failAllocationAfter == 0)
        throw std::bad_alloc();
    if (failAllocationAfter > 0)
        --failAllocationAfter;
    if (void *memory = std::malloc(bytes == 0 ? 1 : bytes))
        return memory;
    throw std::bad_alloc();
}

void *operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete[](void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void *memory, std::size_t) noexcept { std::free(memory); }

namespace
{
using platformator_detail::RenderCullingMode;
using platformator_detail::RenderCullingSpatialKind;
using platformator_detail::RenderCullingState;
using platformator_detail::SpriteRenderBounds;

struct Proxy
{
    SpriteRenderBounds bounds;
    RenderCullingSpatialKind kind;
};

bool intersects(const SpriteRenderBounds &a, const SpriteRenderBounds &b)
{
    return a.maxX >= b.minX && a.minX <= b.maxX && a.maxY >= b.minY && a.minY <= b.maxY;
}

SpriteRenderBounds box(double minX, double minY, double maxX, double maxY)
{
    return {minX, minY, maxX, maxY};
}

size_t addProxy(RenderCullingState &state, const SpriteRenderBounds &bounds, bool certifiable = true)
{
    const size_t rank = state.recordCount();
    state.appendRecord({static_cast<uint32_t>(rank), 1, 1});
    const auto kind = state.updateSpatialBounds(rank, bounds, certifiable);
    return rank;
}

void requireQueryCoverage(RenderCullingState &state, const std::vector<Proxy> &proxies,
                          const SpriteRenderBounds &query)
{
    const auto &result = state.querySpatial(query);
    std::unordered_set<size_t> returned;
    for (const size_t rank : result)
    {
        test_support::require(rank < proxies.size(), "Spatial query returned an out-of-range rank");
        test_support::require(returned.insert(rank).second, "Spatial query returned a duplicate rank");
    }
    for (size_t rank = 0; rank < proxies.size(); ++rank)
    {
        const Proxy &proxy = proxies[rank];
        const bool sideList = proxy.kind != RenderCullingSpatialKind::Indexed;
        if (sideList || intersects(proxy.bounds, query))
            test_support::require(returned.contains(rank), "Spatial query missed an intersecting or conservative proxy");
    }
}

void checkCellCoverageAndConservativeEntries()
{
    RenderCullingState state(RenderCullingMode::Indexed);
    state.reserveRecords(12);
    state.setSpatialCellSize(10.0, 10.0);
    std::vector<Proxy> proxies;
    const auto negativeBounds = box(-10.25, -0.75, -9.75, -0.25);
    addProxy(state, negativeBounds);
    proxies.push_back({negativeBounds, RenderCullingSpatialKind::Indexed});
    const auto tiedBounds = box(9.5, 9.5, 10.5, 10.5);
    addProxy(state, tiedBounds);
    proxies.push_back({tiedBounds, RenderCullingSpatialKind::Indexed});
    const auto oversizedBounds = box(300.0, 300.0, 341.0, 305.0);
    addProxy(state, oversizedBounds);
    proxies.push_back({oversizedBounds, RenderCullingSpatialKind::Oversized});
    const auto uncertainBounds = box(-1.0e30, -1.0e30, 1.0e30, 1.0e30);
    addProxy(state, uncertainBounds, false);
    proxies.push_back({uncertainBounds, RenderCullingSpatialKind::Uncertifiable});

    test_support::require(state.hasSpatialIndex(), "Indexed mode did not construct its spatial index");
    test_support::require(state.spatialCellCapacity() >= 4 * state.recordCount(),
                          "Indexed mode did not reserve actual cell-node supply");
    platformator_test::work = {};
    requireQueryCoverage(state, proxies, box(-10.0, -10.0, -10.0, -10.0));
    requireQueryCoverage(state, proxies, box(9.5, 9.5, 10.5, 10.5));
    requireQueryCoverage(state, proxies, box(900.0, 900.0, 901.0, 901.0));
    test_support::require(platformator_test::work.renderCullingQueries == 3 &&
                              platformator_test::work.renderCullingSideListVisits == 6,
                          "Queries must examine every conservative side-list entry once per query");
    std::cout << "[WORK] index-contract queries=" << platformator_test::work.renderCullingQueries
              << " cellVisits=" << platformator_test::work.renderCullingCellVisits
              << " sideListVisits=" << platformator_test::work.renderCullingSideListVisits
              << " cells=" << state.spatialCellCount() << " nodeSupply=" << state.spatialCellCapacity() << '\n';
    test_support::require(state.validateSpatialInvariants(), "Initial grid links or side-list backlinks are invalid");
}

void checkMovementRemovalAndNodeReuse()
{
    RenderCullingState state(RenderCullingMode::Indexed);
    state.reserveRecords(2);
    state.setSpatialCellSize(10.0, 10.0);
    std::vector<Proxy> proxies;
    for (size_t rank = 0; rank < 5; ++rank)
    {
        const auto bounds = box(static_cast<double>(rank * 30), 0.0, static_cast<double>(rank * 30 + 2), 2.0);
        addProxy(state, bounds);
        proxies.push_back({bounds, RenderCullingSpatialKind::Indexed});
    }
    test_support::require(state.spatialCellCapacity() >= 4 * state.recordCount(),
                          "Record-capacity growth did not prepare its bounded cell-node pool");
    state.clearDirtyQueue();

    const auto moved = box(-20.5, -20.5, -19.5, -19.5);
    proxies[2] = {moved, RenderCullingSpatialKind::Indexed};
    state.updateSpatialBounds(2, moved, true);
    requireQueryCoverage(state, proxies, box(60.0, 0.0, 62.0, 2.0));
    requireQueryCoverage(state, proxies, box(-20.0, -20.0, -20.0, -20.0));

    state.removeRecord(1);
    proxies[1] = proxies.back();
    proxies.pop_back();
    requireQueryCoverage(state, proxies, box(120.0, 0.0, 122.0, 2.0));
    requireQueryCoverage(state, proxies, box(30.0, 0.0, 32.0, 2.0));
    test_support::require(state.recordAt(1).spriteHandle.slot == 4,
                          "Swap removal did not move the final identity into the removed rank");

    const size_t boundedCapacity = state.spatialCellCapacity();
    for (int step = 0; step < 40; ++step)
    {
        const double coordinate = static_cast<double>(1000 + step * 50);
        const auto bounds = box(coordinate, coordinate, coordinate + 1.0, coordinate + 1.0);
        proxies[0] = {bounds, RenderCullingSpatialKind::Indexed};
        state.updateSpatialBounds(0, bounds, true);
        requireQueryCoverage(state, proxies, bounds);
    }
    test_support::require(state.spatialCellCount() <= 4 * state.recordCount() &&
                              state.spatialCellCapacity() == boundedCapacity,
                          "Stable-capacity cell migration retained historical keys or grew the node pool");
    test_support::require(state.validateSpatialInvariants(), "Movement/removal left invalid links or backlinks");
}

void checkHashCollisionsAndQueryStampWrap()
{
    std::unordered_map<size_t, int64_t> firstByHash;
    firstByHash.reserve(300000);
    int64_t firstX = -1;
    int64_t firstY = -1;
    int64_t secondX = -1;
    int64_t secondY = -1;
    for (int64_t coordinate = 0; coordinate < 300000 && firstX < 0; ++coordinate)
    {
        const int64_t x = coordinate % 1000;
        const int64_t y = coordinate / 1000;
        const size_t hash = RenderCullingState::spatialHashForTesting(x, y);
        const auto [it, inserted] = firstByHash.emplace(hash, coordinate);
        if (!inserted && it->second != coordinate)
        {
            firstX = it->second % 1000;
            firstY = it->second / 1000;
            secondX = x;
            secondY = y;
        }
    }
    test_support::require(firstX >= 0, "Could not generate distinct cell keys with a hash collision");
    RenderCullingState state(RenderCullingMode::Indexed);
    state.reserveRecords(2);
    state.setSpatialCellSize(1.0, 1.0);
    const auto firstBounds = box(static_cast<double>(firstX) + 0.1, static_cast<double>(firstY) + 0.1,
                                 static_cast<double>(firstX) + 0.2, static_cast<double>(firstY) + 0.2);
    const auto secondBounds = box(static_cast<double>(secondX) + 0.1, static_cast<double>(secondY) + 0.1,
                                  static_cast<double>(secondX) + 0.2, static_cast<double>(secondY) + 0.2);
    const auto firstRank = addProxy(state, firstBounds);
    const auto secondRank = addProxy(state, secondBounds);
    std::vector<Proxy> proxies{{firstBounds, RenderCullingSpatialKind::Indexed},
                               {secondBounds, RenderCullingSpatialKind::Indexed}};
    platformator_test::work = {};
    state.forceQueryStampForTesting(std::numeric_limits<uint32_t>::max());
    requireQueryCoverage(state, proxies, firstBounds);
    requireQueryCoverage(state, proxies, secondBounds);
    test_support::require(platformator_test::work.renderCullingQueryStampResets == 1,
                          "A forced stamp wrap must reset live stamps exactly once before the query");
    test_support::require(state.recordAt(firstRank).spriteHandle.slot != state.recordAt(secondRank).spriteHandle.slot,
                          "Hash-colliding cell keys aliased distinct record identities");
    test_support::require(state.validateSpatialInvariants(), "Hash collision or stamp wrap damaged grid links");
}

void checkCachedLinearHasNoGrid()
{
    RenderCullingState state(RenderCullingMode::CachedLinear);
    state.reserveRecords(20);
    test_support::require(!state.hasSpatialIndex() && state.spatialCellCount() == 0 &&
                              state.spatialCellCapacity() == 0,
                          "Cached-linear mode must not construct or reserve a spatial grid");
}

void checkOwnedByteObservationIncludesVisibleScratchCapacity(RenderCullingMode mode)
{
    constexpr size_t initialCapacity = 64;
    constexpr size_t grownCapacity = 256;
    constexpr size_t bytesPerRecordCapacity = sizeof(RenderCullingState::Record) +
                                              4 * sizeof(RenderCullingState::RecordIndex) +
                                              sizeof(RenderCullingState::VisibleRankChange);
    const auto requireAccounting = [](const RenderCullingState &state, size_t minimumCapacity,
                                     const char *stage) {
        state.publishCapacityObservation();
        const size_t capacity = platformator_test::work.renderCullingOwnedRecordCapacity;
        test_support::require(capacity >= minimumCapacity,
                              "Owned record capacity is smaller than the requested reserve");
        test_support::require(minimumCapacity <= std::numeric_limits<size_t>::max() / bytesPerRecordCapacity,
                              "Owned-byte lower-bound calculation overflowed");
        const size_t minimum = minimumCapacity * bytesPerRecordCapacity;
        test_support::require(platformator_test::work.renderCullingOwnedBytes >= minimum,
                              std::string("Owned-byte observation omitted visible scratch capacity during ") +
                                  stage);
    };

    RenderCullingState state(mode);
    state.reserveRecords(initialCapacity);
    for (uint32_t slot = 0; slot < initialCapacity; ++slot)
        state.appendRecord({slot, 1, 31});
    requireAccounting(state, initialCapacity, "initial reserve");

    state.beginVisibleSelection();
    for (size_t rank = 0; rank < initialCapacity; ++rank)
        state.appendVisibleRank(rank);
    state.beginVisibleRankChanges();
    for (size_t rank = 0; rank < initialCapacity; ++rank)
        state.queueVisibleRankChange(rank, false);
    state.applyVisibleRankChanges();
    test_support::require(state.visibleRanks().empty(),
                          "The first visibility-change batch did not remove the selected ranks");

    state.beginVisibleSelection();
    state.beginVisibleRankChanges();
    for (size_t rank = 0; rank < initialCapacity; ++rank)
        state.queueVisibleRankChange(rank, true);
    state.applyVisibleRankChanges();
    test_support::require(state.visibleRanks().size() == initialCapacity,
                          "Reused visibility-change storage did not restore every selected rank");
    requireAccounting(state, initialCapacity, "reused visibility-change batch");

    state.reserveRecords(grownCapacity);
    for (uint32_t slot = initialCapacity; slot < grownCapacity; ++slot)
        state.appendRecord({slot, 1, 31});
    requireAccounting(state, grownCapacity, "capacity growth");

    state.releaseStorage();
    test_support::require(platformator_test::work.renderCullingOwnedBytes == 0,
                          "Released render-culling storage retained an owned-byte observation");
}

void checkOwnedByteObservationAccountsCachedAndIndexedBuffers()
{
    checkOwnedByteObservationIncludesVisibleScratchCapacity(RenderCullingMode::CachedLinear);
    checkOwnedByteObservationIncludesVisibleScratchCapacity(RenderCullingMode::Indexed);
}

void checkSwapRemovalPendingDirtyAndGenerationReuse()
{
    RenderCullingState state(RenderCullingMode::Indexed);
    state.reserveRecords(8);
    state.setSpatialCellSize(10.0, 10.0);
    for (uint32_t slot = 0; slot < 6; ++slot)
    {
        const size_t rank = state.recordCount();
        state.appendRecord({slot, 1, 9});
        state.updateSpatialBounds(rank, box(static_cast<double>(slot * 20), 0.0,
                                            static_cast<double>(slot * 20 + 2), 2.0), true);
    }
    state.beginVisibleSelection();
    for (size_t rank = 0; rank < state.recordCount(); ++rank)
        state.appendVisibleRank(rank);
    state.setPreparedKeys({}, {});
    test_support::require(state.dirtyCount() == 6 && state.selectionValid(),
                          "Removal fixture must have a prepared selection and pending dirty ranks");

    state.removeRecord(0); // First rank, with the last record moved into its slot.
    test_support::require(state.recordAt(0).spriteHandle.slot == 5,
                          "First-rank removal did not relocate the last handle");
    state.removeRecord(2); // Middle rank after the first swap.
    const size_t last = state.recordCount() - 1;
    state.removeRecord(last); // Last rank has no relocated record.
    test_support::require(state.dirtyCount() == 3 && state.recordCount() == 3 &&
                              state.validateSpatialInvariants(),
                          "First/middle/last removals must cancel pending entries and preserve backlinks");
    for (size_t queued = 0; queued < state.dirtyCount(); ++queued)
        test_support::require(state.dirtyAt(queued) < state.recordCount(),
                              "Pending dirty queue retained a retired rank after swap removal");

    const auto reusedBounds = box(180.0, 0.0, 182.0, 2.0);
    state.appendRecord({5, 2, 9}); // Same slot, new generation; no object pointer is retained.
    const size_t reusedRank = state.recordCount() - 1;
    state.updateSpatialBounds(reusedRank, reusedBounds, true);
    state.clearDirtyQueue();
    const auto &hits = state.querySpatial(reusedBounds);
    test_support::require(hits.size() == 1 && state.recordAt(hits.front()).spriteHandle ==
                                                        platformator::ObjectHandle{5, 2, 9},
                          "Reused slot/generation must resolve only the replacement handle");
    test_support::require(state.validateSpatialInvariants(),
                          "Generation reuse damaged first/middle/last removal bookkeeping");
}

void checkClusterSpreadAndTransientEmptyReuse()
{
    RenderCullingState state(RenderCullingMode::Indexed);
    state.reserveRecords(32);
    state.setSpatialCellSize(10.0, 10.0);
    for (uint32_t slot = 0; slot < 32; ++slot)
    {
        const size_t rank = state.recordCount();
        state.appendRecord({slot, 1, 17});
        state.updateSpatialBounds(rank, box(1.0 + slot * 0.001, 1.0, 2.0 + slot * 0.001, 2.0), true);
    }
    state.clearDirtyQueue();
    const size_t reservedNodes = state.spatialCellCapacity();
    for (size_t rank = 0; rank < state.recordCount(); ++rank)
        state.updateSpatialBounds(rank, box(1000.0 + rank * 50.0, 1000.0, 1001.0 + rank * 50.0, 1001.0), true);
    test_support::require(state.spatialCellCount() == 32 && state.spatialCellCapacity() == reservedNodes &&
                              state.validateSpatialInvariants(),
                          "Cluster-to-spread migration must reuse the bounded cell/link pools");

    while (state.recordCount() != 0)
        state.removeRecord(state.recordCount() - 1);
    test_support::require(state.spatialCellCount() == 0 && state.spatialCellCapacity() == reservedNodes,
                          "Transiently empty membership must keep prepared capacity for reuse");
    for (uint32_t slot = 0; slot < 32; ++slot)
    {
        const size_t rank = state.recordCount();
        state.appendRecord({slot, 2, 17});
        state.updateSpatialBounds(rank, box(-2000.0 - slot * 40.0, -500.0, -1999.0 - slot * 40.0, -499.0),
                                  true);
    }
    test_support::require(state.spatialCellCount() == 32 && state.spatialCellCapacity() == reservedNodes &&
                              state.validateSpatialInvariants(),
                          "Empty-to-spread churn must not retain old keys or grow stable-capacity pools");
}

void checkCapacityFailureCanRetryWithoutPartialPublication()
{
    const auto makePrepared = [] {
        auto state = std::make_unique<RenderCullingState>(RenderCullingMode::Indexed);
        state->reserveRecords(2);
        state->setSpatialCellSize(10.0, 10.0);
        addProxy(*state, box(1.0, 1.0, 2.0, 2.0));
        addProxy(*state, box(21.0, 1.0, 22.0, 2.0));
        state->clearDirtyQueue();
        state->beginVisibleSelection();
        state->appendVisibleRank(0);
        state->setPreparedKeys({}, {});
        return state;
    };

    auto countState = makePrepared();
    observedAllocations = 0;
    countAllocations = true;
    countState->reserveRecords(64);
    countAllocations = false;
    const size_t allocationSites = observedAllocations;
    test_support::require(allocationSites > 0, "Capacity-reservation fixture did not allocate");

    bool observedFailure = false;
    bool observedSuccess = false;
    for (size_t budget = 0; budget <= allocationSites; ++budget)
    {
        failAllocationAfter = -1;
        auto state = makePrepared();
        failAllocationAfter = static_cast<long>(budget);
        bool failed = false;
        try
        {
            state->reserveRecords(64);
        }
        catch (const std::bad_alloc &)
        {
            failed = true;
        }
        failAllocationAfter = -1;
        observedFailure |= failed;
        observedSuccess |= !failed;
        test_support::require(state->recordCount() == 2 && state->selectionValid() &&
                                  state->visibleRanks() == std::vector<size_t>{0} &&
                                  state->validateSpatialInvariants(),
                              "Failed capacity preparation published a partial selection or broken grid");

        state->reserveRecords(64);
        observedAllocations = 0;
        countAllocations = true;
        for (uint32_t slot = 2; slot < 64; ++slot)
        {
            const size_t rank = state->recordCount();
            state->appendRecord({slot, 1, 21});
            state->updateSpatialBounds(rank, box(200.0 + slot * 20.0, 0.0, 201.0 + slot * 20.0, 1.0), true);
        }
        state->clearDirtyQueue();
        const auto query = box(-10.0, -10.0, 2000.0, 10.0);
        for (int warmup = 0; warmup < 2; ++warmup)
        {
            const auto &candidates = state->querySpatial(query);
            state->beginVisibleSelection();
            for (const size_t rank : candidates)
                state->appendVisibleRank(rank);
            state->sortVisibleRanks();
        }
        observedAllocations = 0;
        countAllocations = true;
        for (int frame = 0; frame < 10; ++frame)
        {
            const auto &candidates = state->querySpatial(query);
            state->beginVisibleSelection();
            for (const size_t rank : candidates)
                state->appendVisibleRank(rank);
            state->sortVisibleRanks();
        }
        countAllocations = false;
        test_support::require(observedAllocations == 0 && state->spatialCellCapacity() >= 4 * 64 &&
                                  state->visibleRanks().size() == 64 && state->validateSpatialInvariants(),
                              "Retried preparation must prime visibility selection and run warmed queries without allocation");
        if (!failed)
            break;
    }
    failAllocationAfter = -1;
    countAllocations = false;
    test_support::require(observedFailure && observedSuccess,
                          "Capacity failure sweep must cover injected failure and successful retry");
}

void run()
{
    checkCellCoverageAndConservativeEntries();
    checkMovementRemovalAndNodeReuse();
    checkHashCollisionsAndQueryStampWrap();
    checkCachedLinearHasNoGrid();
    checkOwnedByteObservationAccountsCachedAndIndexedBuffers();
    checkSwapRemovalPendingDirtyAndGenerationReuse();
    checkClusterSpreadAndTransientEmptyReuse();
    checkCapacityFailureCanRetryWithoutPartialPublication();
}
} // namespace

int main()
{
    try
    {
        run();
        std::cout << "[PASS] render_culling_index_test\n";
        return 0;
    }
    catch (const std::exception &exception)
    {
        std::cerr << "[FAIL] render_culling_index_test: " << exception.what() << '\n';
        return 1;
    }
}
