#include "renderculling.h"
#include "runtimeaccess.h"
#include "sdlwindow.h"

#include <platformator/gameobject.h>
#include <platformator/runtime.h>
#include <platformator/runtimeoptions.h>
#include <platformator/sprite.h>

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

#if PLATFORMATOR_ENABLE_WORK_PROBES
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
#endif // PLATFORMATOR_ENABLE_WORK_PROBES

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
    constexpr size_t bytesPerRecordCapacity =
        sizeof(RenderCullingState::Record) + 4 * sizeof(RenderCullingState::RecordIndex);
    const auto requireAccounting = [mode](const RenderCullingState &state, size_t minimumCapacity,
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
        if (mode == RenderCullingMode::CachedLinear)
        {
            test_support::require(platformator_test::work.renderCullingGridOwnedBytes == 0,
                                  std::string("Cached-linear mode must retain zero grid bytes during ") + stage);
        }
        else if (mode == RenderCullingMode::Indexed)
        {
            test_support::require(platformator_test::work.renderCullingGridOwnedBytes > 0,
                                  std::string("Indexed mode must report positive grid bytes during ") + stage);
        }
    };

    RenderCullingState state(mode);
    state.reserveRecords(initialCapacity);
    for (uint32_t slot = 0; slot < initialCapacity; ++slot)
        state.appendRecord({slot, 1, 31});
    requireAccounting(state, initialCapacity, "initial reserve");

    state.beginVisibleSelectionScratch();
    for (size_t rank = 0; rank < initialCapacity; ++rank)
        state.appendVisibleSelectionScratch(rank);
    state.publishVisibleSelectionScratch();
    state.beginVisibleDelta();
    for (size_t rank = 0; rank < initialCapacity; ++rank)
        state.updateVisibleRank(rank, false);
    test_support::require(state.finishVisibleDelta(), "Expected delta batch to report visibility removal");
    test_support::require(state.visibleRanks().empty() && state.validateMembershipInvariants(),
                          "The first visibility-change batch did not remove the selected ranks");

    state.beginVisibleDelta();
    for (size_t rank = 0; rank < initialCapacity; ++rank)
        state.updateVisibleRank(rank, true);
    test_support::require(state.finishVisibleDelta(), "Expected delta batch to report visibility insertion");
    test_support::require(state.visibleRanks().size() == initialCapacity && state.validateMembershipInvariants(),
                          "Reused visibility-change storage did not restore every selected rank");
    requireAccounting(state, initialCapacity, "reused visibility-change batch");

    state.reserveRecords(grownCapacity);
    for (uint32_t slot = initialCapacity; slot < grownCapacity; ++slot)
        state.appendRecord({slot, 1, 31});
    requireAccounting(state, grownCapacity, "capacity growth");

    state.releaseStorage();
    test_support::require(platformator_test::work.renderCullingOwnedBytes == 0 &&
                              platformator_test::work.renderCullingGridOwnedBytes == 0,
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
    state.beginVisibleSelectionScratch();
    for (size_t rank = 0; rank < state.recordCount(); ++rank)
        state.appendVisibleSelectionScratch(rank);
    state.publishVisibleSelectionScratch();
    state.setPreparedKeys({}, {});
    test_support::require(state.dirtyCount() == 6 && state.selectionValid() && state.validateMembershipInvariants(),
                          "Removal fixture must have a prepared selection and pending dirty ranks");

    state.removeRecord(0); // First rank, with the last record moved into its slot.
    test_support::require(state.recordAt(0).spriteHandle.slot == 5 && state.validateMembershipInvariants(),
                          "First-rank removal did not relocate the last handle or damaged membership");
    state.removeRecord(2); // Middle rank after the first swap.
    test_support::require(state.validateMembershipInvariants(),
                          "Middle-rank removal damaged membership invariants");
    const size_t last = state.recordCount() - 1;
    state.removeRecord(last); // Last rank has no relocated record.
    test_support::require(state.dirtyCount() == 3 && state.recordCount() == 3 &&
                              state.validateSpatialInvariants() && state.validateMembershipInvariants(),
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
        state->beginVisibleSelectionScratch();
        state->appendVisibleSelectionScratch(0);
        state->publishVisibleSelectionScratch();
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
            state->beginVisibleSelectionScratch();
            for (const size_t rank : candidates)
                state->appendVisibleSelectionScratch(rank);
            state->publishVisibleSelectionScratch();
        }
        observedAllocations = 0;
        platformator_test::work = {};
        countAllocations = true;
        for (int frame = 0; frame < 10; ++frame)
        {
            const auto &candidates = state->querySpatial(query);
            state->beginVisibleSelectionScratch();
            for (const size_t rank : candidates)
                state->appendVisibleSelectionScratch(rank);
            state->publishVisibleSelectionScratch();
        }
        countAllocations = false;
        test_support::require(observedAllocations == 0 && state->spatialCellCapacity() >= 4 * 64 &&
                                  state->visibleRanks().size() == 64 && state->validateSpatialInvariants() &&
                                  state->validateMembershipInvariants() &&
                                  platformator_test::work.renderCullingNumericRankSorts == 0 &&
                                  platformator_test::work.visibleMembershipChanges == 0,
                              "Retried preparation must prime visibility selection and run warmed queries without allocation or numeric rank sorts");
        if (!failed)
            break;
    }
    failAllocationAfter = -1;
    countAllocations = false;
    test_support::require(observedFailure && observedSuccess,
                          "Capacity failure sweep must cover injected failure and successful retry");
}

void checkMembershipPublicationAndInversePositions()
{
    RenderCullingState state(RenderCullingMode::CachedLinear);
    state.reserveRecords(4);
    for (uint32_t slot = 0; slot < 4; ++slot)
        state.appendRecord({slot, 1, 5});
    test_support::require(!state.selectionValid(), "Fresh state must start with an invalid selection");
    for (size_t rank = 0; rank < 4; ++rank)
    {
        test_support::require(state.recordAt(rank).visiblePosition == RenderCullingState::absentIndex &&
                                  !state.isRankVisible(rank),
                              "New culling records must start with absent inverse position");
    }

    // 1. Publish an empty first result: must become valid and advance revision once.
    const uint64_t initialRevision = state.visibleMembershipRevision();
    platformator_test::work = {};
    state.beginVisibleSelectionScratch();
    state.publishVisibleSelectionScratch();
    const uint64_t emptyRevision = state.visibleMembershipRevision();
    test_support::require(state.selectionValid() && state.visibleRanks().empty() &&
                              emptyRevision != initialRevision &&
                              platformator_test::work.visibleMembershipChanges == 1 &&
                              state.validateMembershipInvariants(),
                          "First empty publication must mark selection valid and advance revision");

    // 2. Publish {A=0, B=1}: advances revision and sets inverse positions.
    state.beginVisibleSelectionScratch();
    state.appendVisibleSelectionScratch(0);
    state.appendVisibleSelectionScratch(1);
    state.publishVisibleSelectionScratch();
    const uint64_t abRevision = state.visibleMembershipRevision();
    test_support::require(abRevision != emptyRevision && state.visibleRanks() == (std::vector<size_t>{0, 1}) &&
                              state.recordAt(0).visiblePosition == 0 && state.recordAt(1).visiblePosition == 1 &&
                              state.recordAt(2).visiblePosition == RenderCullingState::absentIndex &&
                              state.validateMembershipInvariants(),
                          "Publishing {A,B} did not establish expected membership or inverse positions");

    // 3. Publish {B=1, A=0}: permutation of the same set must preserve revision and skip swap.
    platformator_test::work = {};
    state.beginVisibleSelectionScratch();
    state.appendVisibleSelectionScratch(1);
    state.appendVisibleSelectionScratch(0);
    state.publishVisibleSelectionScratch();
    test_support::require(state.visibleMembershipRevision() == abRevision &&
                              platformator_test::work.visibleMembershipChanges == 0 &&
                              platformator_test::work.visibleMembershipChecks == 2 &&
                              state.visibleRanks() == (std::vector<size_t>{0, 1}) &&
                              state.validateMembershipInvariants(),
                          "Permutation {B,A} must keep the exact same membership revision");

    // 4. Publish {B=1, C=2}: equal-count exchange must change revision and clear A's inverse position.
    platformator_test::work = {};
    state.beginVisibleSelectionScratch();
    state.appendVisibleSelectionScratch(1);
    state.appendVisibleSelectionScratch(2);
    state.publishVisibleSelectionScratch();
    const uint64_t bcRevision = state.visibleMembershipRevision();
    test_support::require(bcRevision != abRevision &&
                              platformator_test::work.visibleMembershipChanges == 1 &&
                              state.visibleRanks() == (std::vector<size_t>{1, 2}) &&
                              state.recordAt(0).visiblePosition == RenderCullingState::absentIndex &&
                              state.recordAt(1).visiblePosition == 0 && state.recordAt(2).visiblePosition == 1 &&
                              state.validateMembershipInvariants(),
                          "Equal-count replacement {B,C} must advance revision and update inverse positions");

    // 5. Delta insertion, no-op, and swap-erase.
    platformator_test::work = {};
    state.beginVisibleDelta();
    state.updateVisibleRank(3, true);
    test_support::require(state.finishVisibleDelta(), "Delta insertion of D must report a change");
    const uint64_t bcdRevision = state.visibleMembershipRevision();
    test_support::require(bcdRevision != bcRevision && state.isRankVisible(3) &&
                              state.recordAt(3).visiblePosition == 2 &&
                              state.visibleRanks() == (std::vector<size_t>{1, 2, 3}) &&
                              state.validateMembershipInvariants(),
                          "Delta insertion of D did not append and assign inverse position");

    platformator_test::work = {};
    state.beginVisibleDelta();
    state.updateVisibleRank(3, true);
    test_support::require(!state.finishVisibleDelta(), "Redundant delta must report no change");
    test_support::require(state.visibleMembershipRevision() == bcdRevision &&
                              platformator_test::work.visibleMembershipChanges == 0,
                          "Delta no-op must not advance revision or count a membership change");

    state.beginVisibleDelta();
    state.updateVisibleRank(1, false);
    test_support::require(state.finishVisibleDelta(), "Delta removal of B must report a change");
    test_support::require(!state.isRankVisible(1) &&
                              state.recordAt(1).visiblePosition == RenderCullingState::absentIndex &&
                              state.visibleRanks() == (std::vector<size_t>{3, 2}) &&
                              state.recordAt(3).visiblePosition == 0 && state.recordAt(2).visiblePosition == 1 &&
                              state.validateMembershipInvariants(),
                          "Delta swap-erase of B did not repair moved rank D's inverse position");

    // 6. Removal of non-last invisible slot (0) when last slot (3) is visible -> 3 relocates to 0.
    state.removeRecord(0);
    test_support::require(state.recordCount() == 3 && state.isRankVisible(0) && state.isRankVisible(2) &&
                              !state.isRankVisible(1) && state.visibleRanks() == (std::vector<size_t>{0, 2}) &&
                              state.validateMembershipInvariants(),
                          "Removing non-last invisible slot did not patch relocated visible last rank");

    // 7. Removal of non-last visible slot (0) when last slot (2) is visible -> 2 relocates to 0.
    state.removeRecord(0);
    test_support::require(state.recordCount() == 2 && state.isRankVisible(0) && !state.isRankVisible(1) &&
                              state.visibleRanks() == (std::vector<size_t>{0}) &&
                              state.recordAt(0).visiblePosition == 0 && state.validateMembershipInvariants(),
                          "Removing non-last visible slot did not erase retired member and patch relocated rank");
}

void checkStableCapacityChurnAndAppendAllocationFailure()
{
    RenderCullingState state(RenderCullingMode::Indexed);
    state.reserveRecords(32);
    state.setSpatialCellSize(10.0, 10.0);
    for (uint32_t slot = 0; slot < 32; ++slot)
        addProxy(state, box(static_cast<double>(slot * 20), 0.0, static_cast<double>(slot * 20 + 2), 2.0));
    state.beginVisibleSelectionScratch();
    for (size_t rank = 0; rank < 16; ++rank)
        state.appendVisibleSelectionScratch(rank);
    state.publishVisibleSelectionScratch();
    state.publishCapacityObservation();
    const size_t initialCap = platformator_test::work.renderCullingOwnedRecordCapacity;
    const size_t initialBytes = platformator_test::work.renderCullingOwnedBytes;

    // Repeated removal + re-append churn within capacity must not grow owned bytes or break invariants.
    for (uint32_t step = 0; step < 64; ++step)
    {
        state.removeRecord(step % 16);
        state.appendRecord({1000u + step, 1, 7});
        state.updateSpatialBounds(state.recordCount() - 1, box(static_cast<double>(step * 20), 0.0,
                                                               static_cast<double>(step * 20 + 2), 2.0), true);
        state.clearDirtyQueue();
        const auto &candidates = state.querySpatial(box(-10.0, -10.0, 400.0, 10.0));
        state.beginVisibleSelectionScratch();
        for (const size_t rank : candidates)
            state.appendVisibleSelectionScratch(rank);
        state.publishVisibleSelectionScratch();
        test_support::require(state.validateSpatialInvariants() && state.validateMembershipInvariants(),
                              "Stable-capacity churn broke spatial or membership invariants");
    }
    state.publishCapacityObservation();
    test_support::require(platformator_test::work.renderCullingOwnedRecordCapacity == initialCap &&
                              platformator_test::work.renderCullingOwnedBytes == initialBytes,
                          "Stable-capacity churn must not grow retained culling capacity or bytes");

    // Failed appendRecord when growth is needed must leave previous records and membership intact.
    failAllocationAfter = 0;
    bool appendFailed = false;
    try
    {
        state.appendRecord({9999u, 1, 7});
    }
    catch (const std::bad_alloc &)
    {
        appendFailed = true;
    }
    failAllocationAfter = -1;
    test_support::require(appendFailed && state.recordCount() == 32 && state.validateSpatialInvariants() &&
                              state.validateMembershipInvariants(),
                          "Failed appendRecord during capacity growth must not leave a partial record");
}

void checkGridSplitIndexedAndCachedLinearParity()
{
    // 1. Cached-linear growth & churn must keep zero grid-specific retained bytes.
    {
        RenderCullingState cached(RenderCullingMode::CachedLinear);
        cached.reserveRecords(16);
        for (uint32_t slot = 0; slot < 16; ++slot)
            cached.appendRecord({slot + 1, 1, 1});
        cached.publishCapacityObservation();
        test_support::require(!cached.hasSpatialIndex() && platformator_test::work.renderCullingGridOwnedBytes == 0 &&
                                  platformator_test::work.renderCullingOwnedBytes > 0,
                              "CachedLinear must own zero grid bytes after initial reserve/append");
        cached.beginVisibleSelectionScratch();
        for (size_t rank = 0; rank < 16; rank += 2)
            cached.appendVisibleSelectionScratch(rank);
        cached.publishVisibleSelectionScratch();
        for (uint32_t step = 0; step < 32; ++step)
        {
            cached.removeRecord(step % 8);
            cached.appendRecord({100u + step, 1, 1});
            test_support::require(cached.validateMembershipInvariants() &&
                                      platformator_test::work.renderCullingGridOwnedBytes == 0,
                                  "CachedLinear churn must preserve membership invariants and zero grid bytes");
        }
        cached.reserveRecords(64);
        for (uint32_t slot = 16; slot < 64; ++slot)
            cached.appendRecord({200u + slot, 1, 1});
        cached.publishCapacityObservation();
        test_support::require(platformator_test::work.renderCullingGridOwnedBytes == 0 &&
                                  platformator_test::work.renderCullingOwnedRecordCapacity >= 64,
                              "CachedLinear growth must still retain zero grid bytes");
        const size_t capacityBoundary = platformator_test::work.renderCullingOwnedRecordCapacity;
        while (cached.recordCount() < capacityBoundary)
            cached.appendRecord({500u + static_cast<uint32_t>(cached.recordCount()), 1, 1});
        for (long failAt = 0; failAt <= 5; ++failAt)
        {
            failAllocationAfter = failAt;
            bool rejected = false;
            try
            {
                cached.appendRecord({999u + static_cast<uint32_t>(failAt), 1, 1});
            }
            catch (const std::bad_alloc &)
            {
                rejected = true;
            }
            failAllocationAfter = -1;
            if (!rejected)
            {
                test_support::require(failAt > 0 && cached.recordCount() == capacityBoundary + 1 &&
                                          cached.validateMembershipInvariants(),
                                      "CachedLinear appendRecord must throw at boundary first and succeed once allocations complete");
                break;
            }
            test_support::require(cached.recordCount() == capacityBoundary && cached.validateMembershipInvariants(),
                                  "CachedLinear allocation failure during appendRecord must preserve prior records");
        }
        if (cached.recordCount() == capacityBoundary)
        {
            cached.appendRecord({1200u, 1, 1});
            test_support::require(cached.recordCount() == capacityBoundary + 1 && cached.validateMembershipInvariants(),
                                  "CachedLinear appendRecord must succeed cleanly on retry after allocation failure");
        }
        cached.releaseStorage();
        test_support::require(platformator_test::work.renderCullingGridOwnedBytes == 0 &&
                                  platformator_test::work.renderCullingOwnedBytes == 0,
                              "CachedLinear releaseStorage must clear owned bytes");
    }

    // 2. Indexed path: all four visible/invisible combinations of retired and moved last slots,
    //    across both cell-indexed and side-listed records, plus stamp wrap and growth alloc failure.
    struct SwapCase
    {
        bool retiredVisible;
        bool movedLastVisible;
        bool retiredIndexed;
        bool movedLastIndexed;
    };
    const SwapCase swapCases[] = {
        {false, false, true, false},
        {false, true, false, true},
        {true, false, true, true},
        {true, true, false, false},
    };
    for (const auto &swapCase : swapCases)
    {
        RenderCullingState indexed(RenderCullingMode::Indexed);
        indexed.reserveRecords(8);
        indexed.setSpatialCellSize(10.0, 10.0);
        for (uint32_t i = 0; i < 4; ++i)
        {
            const SpriteRenderBounds b =
                ((i == 1 && !swapCase.retiredIndexed) || (i == 3 && !swapCase.movedLastIndexed))
                    ? box(0.0, 0.0, 50.0, 50.0) // oversized -> side list
                    : box(static_cast<double>(i * 20), 0.0, static_cast<double>(i * 20 + 2), 2.0);
            addProxy(indexed, b);
        }
        // Rank 0 visible as baseline, rank 2 invisible as baseline.
        // Slot 1 is the retired non-last slot, slot 3 is the moved last slot.
        indexed.beginVisibleSelectionScratch();
        indexed.appendVisibleSelectionScratch(0);
        if (swapCase.retiredVisible)
            indexed.appendVisibleSelectionScratch(1);
        if (swapCase.movedLastVisible)
            indexed.appendVisibleSelectionScratch(3);
        indexed.publishVisibleSelectionScratch();
        const platformator::ObjectHandle expectedMovedHandle = indexed.recordAt(3).spriteHandle;

        indexed.removeRecord(1);
        test_support::require(indexed.recordCount() == 3 &&
                                  indexed.recordAt(1).spriteHandle == expectedMovedHandle &&
                                  indexed.isRankVisible(1) == swapCase.movedLastVisible &&
                                  indexed.isRankVisible(0) && !indexed.isRankVisible(2) &&
                                  indexed.validateSpatialInvariants() && indexed.validateMembershipInvariants(),
                              "Indexed swap removal broke invariants or moved slot visibility");

        indexed.forceQueryStampForTesting(std::numeric_limits<uint32_t>::max());
        const auto &candidates = indexed.querySpatial(box(-5.0, -5.0, 100.0, 100.0));
        test_support::require(candidates.size() == 3 && indexed.validateSpatialInvariants(),
                              "Indexed stamp wrap after split must reset all grid record stamps cleanly");
    }

    // 3. Indexed allocation-failure recovery across early/late allocations during reserveRecords.
    {
        RenderCullingState indexed(RenderCullingMode::Indexed);
        indexed.reserveRecords(8);
        indexed.setSpatialCellSize(10.0, 10.0);
        for (uint32_t i = 0; i < 6; ++i)
            addProxy(indexed, box(static_cast<double>(i * 20), 0.0, static_cast<double>(i * 20 + 2), 2.0));
        for (long failAt = 0; failAt <= 4; ++failAt)
        {
            failAllocationAfter = failAt;
            bool rejected = false;
            try
            {
                indexed.reserveRecords(128);
            }
            catch (const std::bad_alloc &)
            {
                rejected = true;
            }
            failAllocationAfter = -1;
            test_support::require(rejected && indexed.recordCount() == 6 && indexed.validateSpatialInvariants() &&
                                      indexed.validateMembershipInvariants(),
                                  "Allocation failure during grid-split reserve must preserve prior state");
        }
        indexed.reserveRecords(128);
        addProxy(indexed, box(140.0, 0.0, 142.0, 2.0));
        test_support::require(indexed.recordCount() == 7 && indexed.validateSpatialInvariants() &&
                                  indexed.validateMembershipInvariants() &&
                                  platformator_test::work.renderCullingGridOwnedBytes > 0,
                              "Indexed state must recover cleanly after reserve allocation failure");
    }

    // 4. Live SDLWindow registration churn and allocation-failure retry in both cached modes.
    test_support::headlessEnvironment();
    for (const char *mode : {"cached-linear", "indexed"})
    {
        setenv("PLATFORMATOR_RENDER_CULLING_MODE", mode, 1);
        platformator_test::work = {};
        RuntimeOptions options{};
        options.windowSettings.hidden = true;
        options.windowSettings.enableRenderCulling = true;
        platformator::Runtime runtime(options);
        SDLWindow *windowPtr = platformator_detail::RuntimeAccess::window();
        test_support::require(windowPtr != nullptr, std::string(mode) + ": hidden runtime must own SDLWindow");
        SDLWindow &window = *windowPtr;

        std::vector<Sprite *> sprites;
        sprites.reserve(6);
        for (int i = 0; i < 6; ++i)
        {
            GameObject *obj = runtime.createGameObject();
            obj->addComponent<Sprite>();
            sprites.push_back(obj->getComponent<Sprite>());
        }
        // Leave 4 registered (matching initial geometric capacity 4), unregister the remaining 2.
        window.removeSpriteComponent(sprites[4]);
        window.removeSpriteComponent(sprites[5]);

        const size_t initialCap = platformator_test::work.renderCullingOwnedRecordCapacity;
        const size_t initialBytes = platformator_test::work.renderCullingOwnedBytes;
        for (int step = 0; step < 16; ++step)
        {
            Sprite *churnSprite = sprites[step % 2 == 0 ? 3 : 4];
            Sprite *spareSprite = sprites[step % 2 == 0 ? 4 : 3];
            window.removeSpriteComponent(churnSprite);
            window.addSpriteComponent(spareSprite);
        }
        test_support::require(platformator_test::work.renderCullingOwnedRecordCapacity == initialCap &&
                                  platformator_test::work.renderCullingOwnedBytes == initialBytes,
                              std::string(mode) + ": SDLWindow churn within capacity must not grow culling capacity");

        // Trigger capacity growth from 4 -> 8 and force mid-registration allocation failures.
        Sprite *growthSprite = sprites[5];
        for (long failAt = 0; failAt <= 6; ++failAt)
        {
            failAllocationAfter = failAt;
            bool rejected = false;
            try
            {
                window.addSpriteComponent(growthSprite);
            }
            catch (const std::bad_alloc &)
            {
                rejected = true;
            }
            failAllocationAfter = -1;
            if (!rejected)
            {
                // Reached end of allocations needed for this mode; unregister back to 4 for remaining probes.
                window.removeSpriteComponent(growthSprite);
                break;
            }
            test_support::require(!growthSprite->getIsRegisteredInWindow(),
                                  std::string(mode) +
                                      ": failed SDLWindow::addSpriteComponent must leave sprite unregistered");
        }
        window.addSpriteComponent(growthSprite);
        test_support::require(growthSprite->getIsRegisteredInWindow() &&
                                  platformator_test::work.renderCullingOwnedRecordCapacity >= 8 &&
                                  platformator_test::work.spriteOrderOwnedRecords == 5,
                              std::string(mode) +
                                  ": retrying SDLWindow::addSpriteComponent after allocation failure must succeed cleanly");
    }
    unsetenv("PLATFORMATOR_RENDER_CULLING_MODE");
}

void run()
{
    checkCellCoverageAndConservativeEntries();
    checkMovementRemovalAndNodeReuse();
#if PLATFORMATOR_ENABLE_WORK_PROBES
    checkHashCollisionsAndQueryStampWrap();
#endif
    checkCachedLinearHasNoGrid();
    checkOwnedByteObservationAccountsCachedAndIndexedBuffers();
    checkSwapRemovalPendingDirtyAndGenerationReuse();
    checkClusterSpreadAndTransientEmptyReuse();
    checkCapacityFailureCanRetryWithoutPartialPublication();
    checkMembershipPublicationAndInversePositions();
    checkStableCapacityChurnAndAppendAllocationFailure();
    checkGridSplitIndexedAndCachedLinearParity();
}
} // namespace

int main()
{
#if !PLATFORMATOR_ENABLE_WORK_PROBES
    std::cout << "[SKIP] render_culling_index_test requires "
                 "PLATFORMATOR_ENABLE_WORK_PROBES=ON for index and counter observation\n";
    return 77;
#else
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
#endif
}
