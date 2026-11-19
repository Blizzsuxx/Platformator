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
#include <utility>
#include <vector>

#include "runtime_work_probe.h"
#include "test_support.h"

namespace
{
thread_local long failAllocationAfter = -1;
thread_local bool countAllocations = false;
thread_local size_t observedAllocations = 0;
} // namespace

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
using platformator_detail::RenderCullingState;

void checkOwnedByteObservationAccountsCachedBuffers()
{
    platformator_test::work = {};
    constexpr size_t initialCapacity = 32;
    constexpr size_t grownCapacity = 64;
    const size_t bytesPerRecordCapacity =
        sizeof(RenderCullingState::Record) + 3 * sizeof(RenderCullingState::RecordIndex);
    const auto requireAccounting = [&](const RenderCullingState &state, size_t minimumCapacity, const char *stage) {
        test_support::require(platformator_test::work.renderCullingOwnedRecords == state.recordCount(),
                              std::string("Owned record count mismatch during ") + stage);
        test_support::require(platformator_test::work.renderCullingOwnedRecordCapacity >= minimumCapacity,
                              std::string("Owned record capacity too small during ") + stage);
        test_support::require(minimumCapacity <= std::numeric_limits<size_t>::max() / bytesPerRecordCapacity,
                              "Owned-byte lower-bound calculation overflowed");
        const size_t minimum = minimumCapacity * bytesPerRecordCapacity;
        test_support::require(platformator_test::work.renderCullingOwnedBytes >= minimum,
                              std::string("Owned-byte observation omitted visible scratch capacity during ") + stage);
    };

    RenderCullingState state;
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
                              platformator_test::work.renderCullingOwnedRecordCapacity == 0,
                          "Released render-culling storage retained an owned-byte observation");
}

void checkSwapRemovalPendingDirtyAndGenerationReuse()
{
    RenderCullingState state;
    state.reserveRecords(8);
    for (uint32_t slot = 0; slot < 6; ++slot)
        state.appendRecord({slot, 1, 9});
    state.beginVisibleSelectionScratch();
    for (size_t rank = 0; rank < state.recordCount(); ++rank)
        state.appendVisibleSelectionScratch(rank);
    state.publishVisibleSelectionScratch();
    state.setPreparedViewKey({});
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
    test_support::require(state.dirtyCount() == 3 && state.recordCount() == 3 && state.validateMembershipInvariants(),
                          "First/middle/last removals must cancel pending entries and preserve backlinks");
    for (size_t queued = 0; queued < state.dirtyCount(); ++queued)
    {
        test_support::require(state.dirtyAt(queued) < state.recordCount(),
                              "Pending dirty queue retained a retired rank after swap removal");
    }

    state.appendRecord({5, 2, 9}); // Same slot, new generation; no object pointer is retained.
    const size_t reusedRank = state.recordCount() - 1;
    state.clearDirtyQueue();
    test_support::require(state.recordAt(reusedRank).spriteHandle == platformator::ObjectHandle{5, 2, 9} &&
                              state.validateMembershipInvariants(),
                          "Reused slot/generation must publish cleanly under swap relocation");
}

void checkCapacityFailureCanRetryWithoutPartialPublication()
{
    const auto makePrepared = [] {
        auto state = std::make_unique<RenderCullingState>();
        state->reserveRecords(2);
        state->appendRecord({0, 1, 21});
        state->appendRecord({1, 1, 21});
        state->clearDirtyQueue();
        state->beginVisibleSelectionScratch();
        state->appendVisibleSelectionScratch(0);
        state->publishVisibleSelectionScratch();
        state->setPreparedViewKey({});
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
                                  state->validateMembershipInvariants(),
                              "Failed capacity preparation published a partial selection");

        state->reserveRecords(64);
        observedAllocations = 0;
        countAllocations = true;
        for (uint32_t slot = 2; slot < 64; ++slot)
            state->appendRecord({slot, 1, 21});
        state->clearDirtyQueue();
        for (int warmup = 0; warmup < 2; ++warmup)
        {
            state->beginVisibleSelectionScratch();
            for (size_t rank = 0; rank < state->recordCount(); ++rank)
                state->appendVisibleSelectionScratch(rank);
            state->publishVisibleSelectionScratch();
        }
        observedAllocations = 0;
        platformator_test::work = {};
        countAllocations = true;
        for (int frame = 0; frame < 10; ++frame)
        {
            state->beginVisibleSelectionScratch();
            for (size_t rank = 0; rank < state->recordCount(); ++rank)
                state->appendVisibleSelectionScratch(rank);
            state->publishVisibleSelectionScratch();
        }
        countAllocations = false;
        test_support::require(observedAllocations == 0 && state->visibleRanks().size() == 64 &&
                                  state->validateMembershipInvariants() &&
                                  platformator_test::work.renderCullingNumericRankSorts == 0 &&
                                  platformator_test::work.visibleMembershipChanges == 0,
                              "Retried preparation must prime visibility selection and run warmed frames without allocation or numeric rank sorts");
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
    RenderCullingState state;
    state.reserveRecords(4);
    for (uint32_t slot = 0; slot < 4; ++slot)
        state.appendRecord({slot, 1, 5});
    test_support::require(!state.selectionValid(), "Fresh state must start with an invalid selection");
    for (size_t rank = 0; rank < 4; ++rank)
    {
        test_support::require(state.recordAt(rank).visiblePosition == RenderCullingState::absentIndex &&
                                  !state.recordAt(rank).boundsValid &&
                                  !state.isRankVisible(rank),
                              "New culling records must start with absent inverse position and uncertified bounds");
    }

    const platformator_detail::SpriteRenderProjectionKey expectedProjection{1.5f, 2.0f, 320, 240, true};
    const platformator_detail::SpriteRenderViewKey preparedView{
        {0, 1, 9}, 12.0f, -8.0f, expectedProjection, true};
    state.setPreparedViewKey(preparedView);
    test_support::require(&state.projectionKey() == &state.viewKey().projection &&
                              platformator_detail::sameSpriteRenderProjection(state.projectionKey(),
                                                                              expectedProjection) &&
                              platformator_detail::sameSpriteRenderView(state.viewKey(), preparedView),
                          "Cached view key must be the single owner of its projection key");
    state.setPreparedViewKey({});
    test_support::require(!state.projectionKey().valid && !state.viewKey().valid,
                          "Clearing prepared view key must clear its owned projection key");

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
    RenderCullingState state;
    state.reserveRecords(32);
    for (uint32_t slot = 0; slot < 32; ++slot)
        state.appendRecord({slot, 1, 7});
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
        state.clearDirtyQueue();
        state.beginVisibleSelectionScratch();
        for (size_t rank = 0; rank < 16; ++rank)
            state.appendVisibleSelectionScratch(rank);
        state.publishVisibleSelectionScratch();
        test_support::require(state.validateMembershipInvariants(),
                              "Stable-capacity churn broke membership invariants");
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
    test_support::require(appendFailed && state.recordCount() == 32 && state.validateMembershipInvariants(),
                          "Failed appendRecord during capacity growth must not leave a partial record");
}

void checkCachedLinearGrowthRetryAndRelease()
{
    // 1. Standalone cached state growth, churn, allocation-failure recovery, and release.
    {
        RenderCullingState cached;
        cached.reserveRecords(16);
        for (uint32_t slot = 0; slot < 16; ++slot)
            cached.appendRecord({slot + 1, 1, 1});
        cached.publishCapacityObservation();
        test_support::require(platformator_test::work.renderCullingOwnedBytes > 0,
                              "CachedLinear must report retained common storage after initial reserve/append");
        cached.beginVisibleSelectionScratch();
        for (size_t rank = 0; rank < 16; rank += 2)
            cached.appendVisibleSelectionScratch(rank);
        cached.publishVisibleSelectionScratch();
        for (uint32_t step = 0; step < 32; ++step)
        {
            cached.removeRecord(step % 8);
            cached.appendRecord({100u + step, 1, 1});
            test_support::require(cached.validateMembershipInvariants(),
                                  "CachedLinear churn must preserve membership invariants");
        }
        cached.reserveRecords(64);
        for (uint32_t slot = 16; slot < 64; ++slot)
            cached.appendRecord({200u + slot, 1, 1});
        cached.publishCapacityObservation();
        test_support::require(platformator_test::work.renderCullingOwnedRecordCapacity >= 64,
                              "CachedLinear growth must expand record capacity cleanly");
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
        test_support::require(platformator_test::work.renderCullingOwnedBytes == 0,
                              "CachedLinear releaseStorage must clear owned bytes");
    }

    // 2. Live SDLWindow registration churn and allocation-failure retry in cached-linear mode.
    test_support::headlessEnvironment();
    for (const char *mode : {static_cast<const char *>(nullptr), "cached-linear"})
    {
        if (mode == nullptr)
            test_support::clearEnvironment("PLATFORMATOR_RENDER_CULLING_MODE");
        else
            test_support::setEnvironment("PLATFORMATOR_RENDER_CULLING_MODE", mode);
        platformator_test::work = {};
        RuntimeOptions options{};
        options.windowSettings.hidden = true;
        options.windowSettings.enableRenderCulling = true;
        platformator::Runtime runtime(options);
        SDLWindow *windowPtr = platformator_detail::RuntimeAccess::window();
        test_support::require(windowPtr != nullptr, "Hidden runtime must own SDLWindow");
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
                              "SDLWindow churn within capacity must not grow culling capacity");

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
                window.removeSpriteComponent(growthSprite);
                break;
            }
            test_support::require(!growthSprite->getIsRegisteredInWindow(),
                                  "Failed SDLWindow::addSpriteComponent must leave sprite unregistered");
        }
        window.addSpriteComponent(growthSprite);
        test_support::require(growthSprite->getIsRegisteredInWindow() &&
                                  platformator_test::work.renderCullingOwnedRecordCapacity >= 8 &&
                                  platformator_test::work.spriteOrderOwnedRecords == 5,
                              "Retrying SDLWindow::addSpriteComponent after allocation failure must succeed cleanly");
    }
    test_support::clearEnvironment("PLATFORMATOR_RENDER_CULLING_MODE");
}

void run()
{
    checkOwnedByteObservationAccountsCachedBuffers();
    checkSwapRemovalPendingDirtyAndGenerationReuse();
    checkCapacityFailureCanRetryWithoutPartialPublication();
    checkMembershipPublicationAndInversePositions();
    checkStableCapacityChurnAndAppendAllocationFailure();
    checkCachedLinearGrowthRetryAndRelease();
}
} // namespace

int main()
{
#if !PLATFORMATOR_ENABLE_WORK_PROBES
    std::cout << "[SKIP] render_culling_state_test requires "
                 "PLATFORMATOR_ENABLE_WORK_PROBES=ON for culling state and counter observation\n";
    return 77;
#else
    try
    {
        run();
        std::cout << "[PASS] render_culling_state_test\n";
        return 0;
    }
    catch (const std::exception &exception)
    {
        std::cerr << "[FAIL] render_culling_state_test: " << exception.what() << '\n';
        return 1;
    }
#endif
}
