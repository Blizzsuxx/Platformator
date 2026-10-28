#include "physicsmanager.h"
#include "runtimeaccess.h"
#include "spatialindex.h"
#include "test_support.h"
#include "spatial_test_support.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <new>
#include <platformator/boxcollider.h>
#include <type_traits>

#include "aabb.h"

using namespace test_support;
using namespace spatial_test_support;
using platformator_detail::SpatialBounds;

namespace
{
std::atomic<bool> countSpatialAllocations{false};
std::atomic<size_t> spatialAllocations{0};
} // namespace

void *operator new(size_t bytes)
{
    if (countSpatialAllocations.load(std::memory_order_relaxed))
        spatialAllocations.fetch_add(1, std::memory_order_relaxed);
    if (void *memory = std::malloc(bytes ? bytes : 1))
        return memory;
    throw std::bad_alloc();
}

void *operator new[](size_t bytes)
{
    return ::operator new(bytes);
}

void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete[](void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, size_t) noexcept { std::free(memory); }
void operator delete[](void *memory, size_t) noexcept { std::free(memory); }

static_assert(std::is_trivially_copyable_v<ProjectionOwner>);
static_assert(sizeof(ProjectionOwner) <= sizeof(void *) * 2);

namespace
{
struct BoxProxy
{
    GameObject *object;
    BoxCollider *collider;
    float width;
    float height;
    uint64_t context;
};

struct VisibilityRecord
{
    SpatialBounds bounds;
    BoundingRadiusProjectionAxis x;
    BoundingRadiusProjectionAxis y;

    explicit VisibilityRecord(SpatialBounds bounds)
        : bounds(bounds), x(this, ProjectionOwnerKind::Visibility, bounds.minX, bounds.maxX),
          y(this, ProjectionOwnerKind::Visibility, bounds.minY, bounds.maxY)
    {
    }

    ProjectionOwner endpoint() { return {this, ProjectionOwnerKind::Visibility}; }

    void setBounds(SpatialBounds next)
    {
        bounds = next;
        x.getMin()->setProjectedPosition(next.minX);
        x.getMax()->setProjectedPosition(next.maxX);
        y.getMin()->setProjectedPosition(next.minY);
        y.getMax()->setProjectedPosition(next.maxY);
    }
};

struct PairEventCounts
{
    size_t added = 0;
    size_t removed = 0;
    std::map<std::pair<const void *, const void *>, int,
             std::function<bool(const std::pair<const void *, const void *> &,
                                const std::pair<const void *, const void *> &)>> supports{
        [](const auto &a, const auto &b) {
            const std::less<const void *> less;
            return less(a.first, b.first) || (a.first == b.first && less(a.second, b.second));
        }};
};

std::pair<const void *, const void *> canonicalOwnerPair(ProjectionOwner a, ProjectionOwner b)
{
    const std::less<const void *> less;
    return less(a.pointer, b.pointer) ? std::make_pair(a.pointer, b.pointer) : std::make_pair(b.pointer, a.pointer);
}

bool visibilityPairEligible(void *, ProjectionOwner a, ProjectionOwner b)
{
    return a.kind == ProjectionOwnerKind::Visibility && b.kind == ProjectionOwnerKind::Visibility;
}

void recordVisibilityPairChange(void *context, ProjectionOwner a, ProjectionOwner b, int delta)
{
    auto &counts = *static_cast<PairEventCounts *>(context);
    if (a.kind != ProjectionOwnerKind::Visibility || b.kind != ProjectionOwnerKind::Visibility)
        throw std::runtime_error("AABB pair policy dispatched an endpoint before checking its owner kind");
    if (delta > 0)
        counts.added += static_cast<size_t>(delta);
    else
        counts.removed += static_cast<size_t>(-delta);
    auto &witnesses = counts.supports[canonicalOwnerPair(a, b)];
    witnesses += delta;
    if (witnesses < 0)
        throw std::runtime_error("Generic segmented pair policy removed a missing cell witness");
    if (witnesses == 0)
        counts.supports.erase(canonicalOwnerPair(a, b));
}

void verifyGenericSupports(const std::vector<std::unique_ptr<VisibilityRecord>> &records,
                           const PairEventCounts &events, int expectedWitnesses)
{
    std::map<std::pair<const void *, const void *>, int,
             std::function<bool(const std::pair<const void *, const void *> &,
                                const std::pair<const void *, const void *> &)>> expected{
        [](const auto &a, const auto &b) {
            const std::less<const void *> less;
            return less(a.first, b.first) || (a.first == b.first && less(a.second, b.second));
        }};
    for (size_t i = 0; i < records.size(); ++i)
    {
        for (size_t j = i + 1; j < records.size(); ++j)
        {
            if (records[i]->bounds.overlapsInclusive(records[j]->bounds))
                expected.emplace(canonicalOwnerPair(records[i]->endpoint(), records[j]->endpoint()), expectedWitnesses);
        }
    }
    require(events.supports == expected,
            "Generic cell witnesses match the independent inclusive exact-overlap oracle");
}

Bounds currentBounds(const BoxProxy &proxy)
{
    const auto position = proxy.object->getPosition();
    return {position.x() - proxy.width * 0.5, position.y() - proxy.height * 0.5,
            position.x() + proxy.width * 0.5, position.y() + proxy.height * 0.5};
}

void verifyAgainstExactReference(const std::vector<BoxProxy> &boxes)
{
    std::vector<EndpointState> endpoints;
    endpoints.reserve(boxes.size());
    for (const auto &box : boxes)
    {
        endpoints.push_back({{box.context, box.collider->getHandle()}, currentBounds(box),
                             box.collider->getCollisionGroup(), box.collider->getCollisionMask(),
                             box.object->getActive()});
    }
    const auto expected = exactOverlapOracle(endpoints);
    const auto *physics = platformator_detail::RuntimeAccess::physicsManager();
    require(physics->getGrid().getCandidatePairCount() == expected.size(),
            "Segmented grid candidate count matches independent exact inclusive overlap oracle");
    for (size_t i = 0; i < boxes.size(); ++i)
    {
        const bool shouldBeRegistered = boxes[i].object->getActive();
        require(boxes[i].collider->getIsRegisteredInBroadPhase() == shouldBeRegistered &&
                    boxes[i].collider->getIsRegisteredInGrid() == shouldBeRegistered,
                "Segmented registration reports active broad-phase and literal Grid membership");
        for (size_t j = i + 1; j < boxes.size(); ++j)
        {
            const auto &a = endpoints[i];
            const auto &b = endpoints[j];
            const auto pair = CanonicalPair::make(a.identity, b.identity);
            const bool want = std::binary_search(expected.begin(), expected.end(), pair);
            require(physics->getGrid().containsCellPair(boxes[i].collider, boxes[j].collider) == want,
                    "Segmented pair support agrees with exact current bounds and both filter directions");
        }
    }
}

void contextIdentityIsIndependent()
{
    const auto handle = [](uint32_t slot) { return platformator::ObjectHandle{slot, 4, 91}; };
    std::vector<EndpointState> endpoints{
        {{10, handle(0)}, {0, 0, 4, 4}, 1, UINT64_MAX, true},
        {{10, handle(1)}, {4, 0, 8, 4}, 1, 1, true},
        {{20, handle(0)}, {0, 0, 4, 4}, 1, UINT64_MAX, true},
        {{20, handle(1)}, {100, 0, 104, 4}, 1, 1, true}};
    const auto expected = exactOverlapOracle(endpoints);
    require(expected.size() == 1 && expected.front().first.context == 10,
            "A visibility context cannot inherit physics candidates for the same object handles");
}

void boundsAndRuntimeContextContracts()
{
    using platformator_detail::SpatialBounds;
    using platformator_detail::SpatialContext;
    using platformator_detail::SpatialDomain;

    const SpatialBounds exact{0.1f, -2, 4, 5};
    const auto envelope = exact.padded(0.2f);
    require(exact.isFiniteOrdered() && envelope.isFiniteOrdered() && envelope.minX <= exact.minX &&
                envelope.minY <= exact.minY && envelope.maxX >= exact.maxX && envelope.maxY >= exact.maxY,
            "Padded tree envelopes conservatively contain the exact current bounds");
    require(SpatialBounds{0, 0, 1, 1}.overlapsInclusive({1, 1, 2, 2}),
            "Bounds use inclusive edge and corner overlap");
    require(!SpatialBounds{2, 0, 1, 1}.isFiniteOrdered(), "Reversed bounds are rejected");

    SpatialContext physics(91, SpatialDomain::Physics);
    SpatialContext visibility(91, SpatialDomain::Visibility);
    const platformator::ObjectHandle handle{4, 7, 91};
    const auto physicsIdentity = physics.identify(handle);
    const auto visibilityIdentity = visibility.identify(handle);
    require(physicsIdentity != visibilityIdentity && physics.owns(physicsIdentity) &&
                visibility.owns(visibilityIdentity) && !physics.owns(visibilityIdentity),
            "Equal component handles remain isolated between runtime spatial contexts");

    bool rejected = false;
    try
    {
        (void)physics.identify({4, 8, 92});
    }
    catch (const std::invalid_argument &)
    {
        rejected = true;
    }
    require(rejected, "Spatial identity creation rejects a component from another runtime");

    rejected = false;
    try
    {
        (void)exact.padded(-1);
    }
    catch (const std::invalid_argument &)
    {
        rejected = true;
    }
    require(rejected, "Spatial padding must be finite and nonnegative");
}

void projectionOwnerKinds()
{
    int spriteRecord = 0;
    BoundingRadiusProjectionAxis visibility(&spriteRecord, ProjectionOwnerKind::Visibility, -3, 7);
    auto *minimum = visibility.getMin();
    auto *maximum = visibility.getMax();
    require(minimum->getOwner() == &spriteRecord && maximum->getOwner() == &spriteRecord,
            "Generic interval endpoints retain the stable opaque owner pointer");
    require(minimum->getOwnerKind() == ProjectionOwnerKind::Visibility &&
                maximum->getOwnerKind() == ProjectionOwnerKind::Visibility,
            "Generic interval endpoints carry a compact owner kind");
    require(minimum->getCollider() == nullptr && maximum->getCollider() == nullptr,
            "Visibility interval endpoints never cast their owner to Collider");
    require(*minimum < *maximum, "Owner dispatch data does not alter min-before-max endpoint ordering");
}

void genericSegmentedCellOperations()
{
    PairEventCounts events;
    const AABB::PairPolicy policy{&events, ProjectionOwnerKind::Visibility, &visibilityPairEligible,
                                  &recordVisibilityPairChange};
    GridCell firstCell(GridCellKey{0, 0}, policy);
    GridCell secondCell(GridCellKey{1, 0}, policy);
    std::vector<std::unique_ptr<VisibilityRecord>> records;
    records.reserve(42);
    records.push_back(std::make_unique<VisibilityRecord>(SpatialBounds{-10, 0, 400, 10}));
    for (int i = 0; i < 41; ++i)
        records.push_back(std::make_unique<VisibilityRecord>(SpatialBounds{i * 10.f, 2, i * 10.f + 1, 8}));

    for (const auto &record : records)
    {
        firstCell.addEndpoint(record->endpoint(), &record->x, &record->y);
        secondCell.addEndpoint(record->endpoint(), &record->x, &record->y);
    }

    require(firstCell.getAABB()->getIntervalListX()->getChunkCount() > 1,
            "Generic endpoint intervals retain the segmented chunk split algorithm");
    require(events.added == 82 && events.removed == 0,
            "Two independent cells report one witness each for the 41 exact giant-interval overlaps");
    verifyGenericSupports(records, events, 2);

    records[11]->setBounds({1000, 2, 1001, 8});
    firstCell.repairEndpoint(records[11]->endpoint());
    secondCell.repairEndpoint(records[11]->endpoint());
    require(events.removed == 2,
            "A fast endpoint move removes both old cell witnesses after interval repair");
    verifyGenericSupports(records, events, 2);

    // At x=400, min-before-max ordering preserves the inclusive edge contact with the long interval.
    records[11]->setBounds({400, 2, 401, 8});
    firstCell.repairEndpoint(records[11]->endpoint());
    secondCell.repairEndpoint(records[11]->endpoint());
    require(events.added == 86,
            "Touching min/max ties remain candidates for non-Collider visibility owners (added=" +
                std::to_string(events.added) + ")");
    verifyGenericSupports(records, events, 2);

    const auto removedBeforeRetire = events.removed;
    for (const auto &record : records)
    {
        firstCell.removeEndpoint(record->endpoint());
        secondCell.removeEndpoint(record->endpoint());
    }
    require(events.removed > removedBeforeRetire,
            "Generic endpoint removal dispatches final candidate witness removals before owner retirement");
    require(events.supports.empty(), "Cell removal leaves no generic endpoint candidate witnesses");
}

void genericSegmentedContextOperations()
{
    PairEventCounts events;
    platformator_detail::SegmentedSpatialContext context(
        100, {&events, ProjectionOwnerKind::Visibility, &visibilityPairEligible, &recordVisibilityPairChange});
    const size_t emptyCapacity = context.getOwnedCapacityBytes();
    std::vector<std::unique_ptr<VisibilityRecord>> records;
    records.reserve(42);
    records.push_back(std::make_unique<VisibilityRecord>(SpatialBounds{-10, 0, 400, 10}));
    for (int i = 0; i < 41; ++i)
        records.push_back(std::make_unique<VisibilityRecord>(SpatialBounds{i * 10.f, 2, i * 10.f + 1, 8}));
    for (const auto &record : records)
        context.insert(record->endpoint(), &record->x, &record->y, record->bounds);

    require(context.getCandidatePairCount() == 41 && context.getCellCount() > 1 &&
                context.getCellMembershipCount(records.front()->endpoint()) == 6,
            "The owner-scoped segmented context builds independent cells and exact deduplicated candidates");
    verifyGenericSupports(records, events, 1);
    const size_t populatedCapacity = context.getOwnedCapacityBytes();
    require(populatedCapacity > emptyCapacity + records.size() * sizeof(AABB::ColliderBinding) +
                context.getCellCount() * sizeof(GridCell),
            "Capacity covers owned cells, bindings, segmented chunks and checkpoint/container payloads");

    context.update(records[12]->endpoint(), records[12]->bounds); // Warm reusable cell-key scratch.
    spatialAllocations = 0;
    countSpatialAllocations = true;
    for (int i = 0; i < 100; ++i)
        context.update(records[12]->endpoint(), records[12]->bounds);
    countSpatialAllocations = false;
    require(spatialAllocations == 0,
            "A warmed unchanged segmented endpoint update reuses cell-key and interval scratch storage");

    records[11]->setBounds({1000, 2, 1001, 8});
    context.update(records[11]->endpoint(), records[11]->bounds);
    require(context.getCandidatePairCount() == 40,
            "A large migration updates cells and removes the moved endpoint's old pair");
    verifyGenericSupports(records, events, 1);

    records[11]->setBounds({400, 2, 401, 8});
    context.update(records[11]->endpoint(), records[11]->bounds);
    require(context.getCandidatePairCount() == 42 &&
                context.containsPair(records.front()->endpoint(), records[11]->endpoint()),
            "Reinsertion across cell boundaries restores exact inclusive contacts without duplicate candidates");
    verifyGenericSupports(records, events, 1);

    for (const auto &record : records)
        context.remove(record->endpoint());
    require(context.getCandidatePairCount() == 0 && context.getCellCount() == 0 && events.supports.empty(),
            "Removing the context's endpoints reclaims its empty cells and pair witnesses");
    require(context.getOwnedCapacityBytes() < populatedCapacity && context.getOwnedCapacityBytes() > emptyCapacity,
            "Removing cells releases their payload while retained hash/scratch capacity remains accounted");
    std::cout << "spatial layout projection=" << sizeof(BoundingRadiusProjection)
              << " proxy=" << sizeof(BoundingRadiusProjectionProxy)
              << " axis_binding=" << sizeof(BoundingRadiusProjectionAxisBinding)
              << " cell_binding=" << sizeof(AABB::ColliderBinding) << " warmed_update_allocations="
              << spatialAllocations << '\n';
}

void segmentedOperations()
{
    RuntimeFixture fixture(1);
    auto &runtime = fixture.getRuntime();
    std::vector<BoxProxy> boxes;
    boxes.reserve(100);

    auto add = [&](float x, float y, float width, float height) {
        auto *object = runtime.createGameObject()->setPosition({x, y});
        auto *collider = object->addComponent<BoxCollider>(width, height)->getComponent<BoxCollider>();
        boxes.push_back({object, collider, width, height, 10});
    };

    // Many endpoints force chunk creation; the long proxy overlaps across many chunks and cells.
    add(0, 0, 1200, 600);
    add(-610, 0, 20, 20); // inclusive edge touch with the long proxy
    add(610, 0, 20, 20);
    for (int i = 0; i < 80; ++i)
        add(-1000.f + i * 25.f, (i % 5) * 30.f, i % 9 == 0 ? 180.f : 12.f, 12.f);
    runtime.simulateFrame(dt);
    verifyAgainstExactReference(boxes);

    // Fast motion crosses many chunk boundaries and cell memberships in each direction.
    boxes[37].object->setPosition({1400, -300});
    runtime.simulateFrame(dt);
    verifyAgainstExactReference(boxes);
    boxes[37].object->setPosition({-1400, 300});
    runtime.simulateFrame(dt);
    verifyAgainstExactReference(boxes);

    // Filter changes and activation alter eligibility without changing endpoint geometry.
    boxes[20].collider->setCollisionGroup(UINT64_C(1) << 63);
    boxes[21].collider->setCollisionMask(UINT64_C(1) << 63);
    runtime.simulateFrame(dt);
    verifyAgainstExactReference(boxes);
    boxes[21].object->setActive(false);
    runtime.simulateFrame(dt);
    verifyAgainstExactReference(boxes);
    boxes[21].object->setActive(true);
    runtime.simulateFrame(dt);
    verifyAgainstExactReference(boxes);

    // Insert/remove churn exercises chunk split/merge and multi-cell witness cancellation.
    for (int i = 0; i < 24; ++i)
        add(2500.f + i * 5.f, -1200.f, 80.f, 80.f);
    runtime.simulateFrame(dt);
    verifyAgainstExactReference(boxes);
    for (int i = 0; i < 32; ++i)
    {
        boxes[i + 12].object->destroy();
        boxes[i + 12].object = nullptr;
        boxes[i + 12].collider = nullptr;
    }
    boxes.erase(std::remove_if(boxes.begin(), boxes.end(), [](const BoxProxy &box) { return !box.object; }),
                boxes.end());
    runtime.simulateFrame(dt);
    verifyAgainstExactReference(boxes);
}
} // namespace

int main()
{
    try
    {
        contextIdentityIsIndependent();
        boundsAndRuntimeContextContracts();
        projectionOwnerKinds();
        genericSegmentedCellOperations();
        genericSegmentedContextOperations();
        segmentedOperations();
        std::cout << "Spatial segmented regressions passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
