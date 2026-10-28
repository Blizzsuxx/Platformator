#include "spatialindex.h"
#include "benchmark.h"
#include "spatialresourcehooks.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <oneapi/tbb.h>
#include <utility>
#include <vector>

#include "aabb.h"
#include "dynamicaabbtree.h"
#include "gridcell.h"
#include "physicsmanager.h"
#include "physicspairstate.h"
#include "platformator/gridtypes.h"
#include "runtimeaccess.h"
#include "workprobe.h"

namespace platformator_detail
{
namespace
{
std::atomic<uint64_t> nextSpatialContextId{1};
}

bool SpatialBounds::isFiniteOrdered() const
{
    return std::isfinite(minX) && std::isfinite(minY) && std::isfinite(maxX) && std::isfinite(maxY) && minX <= maxX &&
           minY <= maxY;
}

bool SpatialBounds::overlapsInclusive(const SpatialBounds &other) const
{
    return minX <= other.maxX && maxX >= other.minX && minY <= other.maxY && maxY >= other.minY;
}

SpatialBounds SpatialBounds::padded(float padding) const
{
    if (!isFiniteOrdered())
        throw std::invalid_argument("Spatial bounds must be finite and ordered");
    if (!std::isfinite(padding) || padding < 0)
        throw std::invalid_argument("Spatial bounds padding must be finite and nonnegative");

    const double lowX = static_cast<double>(minX) - padding;
    const double lowY = static_cast<double>(minY) - padding;
    const double highX = static_cast<double>(maxX) + padding;
    const double highY = static_cast<double>(maxY) + padding;
    float resultMinX = static_cast<float>(lowX);
    float resultMinY = static_cast<float>(lowY);
    float resultMaxX = static_cast<float>(highX);
    float resultMaxY = static_cast<float>(highY);
    if (static_cast<double>(resultMinX) > lowX)
        resultMinX = std::nextafter(resultMinX, -std::numeric_limits<float>::infinity());
    if (static_cast<double>(resultMinY) > lowY)
        resultMinY = std::nextafter(resultMinY, -std::numeric_limits<float>::infinity());
    if (static_cast<double>(resultMaxX) < highX)
        resultMaxX = std::nextafter(resultMaxX, std::numeric_limits<float>::infinity());
    if (static_cast<double>(resultMaxY) < highY)
        resultMaxY = std::nextafter(resultMaxY, std::numeric_limits<float>::infinity());
    SpatialBounds result{resultMinX, resultMinY, resultMaxX, resultMaxY};
    if (!result.isFiniteOrdered())
        throw std::invalid_argument("Padded spatial bounds are not finite");
    return result;
}

size_t SpatialIdentityHash::operator()(const SpatialIdentity &identity) const noexcept
{
    size_t result = std::hash<uint64_t>{}(identity.contextId);
    const auto combine = [&result](uint64_t value) {
        result ^= std::hash<uint64_t>{}(value) + size_t(0x9e3779b9) + (result << 6) + (result >> 2);
    };
    combine(identity.endpoint.runtimeSerial);
    combine(identity.endpoint.slot);
    combine(identity.endpoint.generation);
    return result;
}

struct SegmentedSpatialContext::Impl
{
    struct EndpointMembership
    {
        BoundingRadiusProjectionAxis *xAxis;
        BoundingRadiusProjectionAxis *yAxis;
        SpatialBounds bounds;
        std::vector<GridCellKey> cells;
    };

    float cellSize;
    SpatialPairPolicy pairPolicy;
    AABB::PairPolicy cellPairPolicy;
    std::unordered_map<GridCellKey, std::unique_ptr<GridCell>, GridCellKey::Hash> cells;
    std::unordered_map<ProjectionOwner, EndpointMembership, ProjectionOwnerHash> endpoints;
    std::unordered_map<AABBPair, size_t, AABBPair::HashFunction> pairWitnesses;
    std::vector<GridCellKey> cellKeyScratch;

    Impl(float cellSize, SpatialPairPolicy pairPolicy)
        : cellSize(cellSize), pairPolicy(pairPolicy),
          cellPairPolicy{this, pairPolicy.endpointKind, &Impl::eligible, &Impl::pairWitnessChanged}, cells(),
          endpoints(), pairWitnesses(), cellKeyScratch()
    {
        if (!std::isfinite(cellSize) || cellSize <= 0)
            throw std::invalid_argument("Segmented spatial cell size must be finite and positive");
    }

    static bool eligible(void *context, ProjectionOwner a, ProjectionOwner b)
    {
        auto &self = *static_cast<Impl *>(context);
        if (a.kind != self.pairPolicy.endpointKind || b.kind != self.pairPolicy.endpointKind)
            return false;
        return self.pairPolicy.eligible == nullptr ||
               self.pairPolicy.eligible(self.pairPolicy.context, a, b);
    }

    static void pairWitnessChanged(void *context, ProjectionOwner a, ProjectionOwner b, int delta)
    {
        auto &self = *static_cast<Impl *>(context);
        const AABBPair key(a, b);
        auto found = self.pairWitnesses.find(key);
        if (delta > 0)
        {
            if (found == self.pairWitnesses.end())
            {
                self.pairWitnesses.emplace(key, static_cast<size_t>(delta));
                if (self.pairPolicy.changed)
                    self.pairPolicy.changed(self.pairPolicy.context,
                                            {key.a, self.pairPolicy.endpointKind},
                                            {key.b, self.pairPolicy.endpointKind}, 1);
            }
            else
            {
                found->second += static_cast<size_t>(delta);
            }
            return;
        }

        if (found == self.pairWitnesses.end() || found->second < static_cast<size_t>(-delta))
            throw std::logic_error("Segmented spatial context received an unmatched cell witness removal");
        found->second -= static_cast<size_t>(-delta);
        if (found->second == 0)
        {
            self.pairWitnesses.erase(found);
            if (self.pairPolicy.changed)
                self.pairPolicy.changed(self.pairPolicy.context,
                                        {key.a, self.pairPolicy.endpointKind},
                                        {key.b, self.pairPolicy.endpointKind}, -1);
        }
    }

    void getKeys(const SpatialBounds &bounds, std::vector<GridCellKey> &keys) const
    {
        if (!bounds.isFiniteOrdered())
            throw std::invalid_argument("Segmented spatial bounds must be finite and ordered");
        const auto coordinate = [&](float value) {
            const double cell = std::floor(static_cast<double>(value) / static_cast<double>(cellSize));
            if (cell < static_cast<double>(std::numeric_limits<int>::min()) ||
                cell > static_cast<double>(std::numeric_limits<int>::max()))
                throw std::out_of_range("Segmented spatial cell coordinate exceeds integer range");
            return static_cast<int>(cell);
        };
        const int minX = coordinate(bounds.minX);
        const int maxX = coordinate(bounds.maxX);
        const int minY = coordinate(bounds.minY);
        const int maxY = coordinate(bounds.maxY);

        keys.clear();
        const uint64_t width = static_cast<uint64_t>(static_cast<int64_t>(maxX) - minX + 1);
        const uint64_t height = static_cast<uint64_t>(static_cast<int64_t>(maxY) - minY + 1);
        if (height != 0 && width > static_cast<uint64_t>(std::numeric_limits<size_t>::max()) / height)
            throw std::length_error("Segmented spatial cell range is too large");
        keys.reserve(static_cast<size_t>(width * height));
        for (int64_t x = minX; x <= maxX; ++x)
            for (int64_t y = minY; y <= maxY; ++y)
                keys.emplace_back(static_cast<int>(x), static_cast<int>(y));
    }

    GridCell &getOrCreateCell(GridCellKey key)
    {
        auto found = cells.find(key);
        if (found != cells.end())
            return *found->second;
        auto cell = std::make_unique<GridCell>(key, cellPairPolicy);
        auto *result = cell.get();
        cells.emplace(key, std::move(cell));
        return *result;
    }

    void addToCell(GridCellKey key, ProjectionOwner endpoint, const EndpointMembership &membership)
    {
        getOrCreateCell(key).addEndpoint(endpoint, membership.xAxis, membership.yAxis);
    }

    void removeFromCell(GridCellKey key, ProjectionOwner endpoint)
    {
        auto found = cells.find(key);
        if (found == cells.end())
            return;
        found->second->removeEndpoint(endpoint);
        if (found->second->getAABB()->getIsEmpty())
            cells.erase(found);
    }

    void repairInCell(GridCellKey key, ProjectionOwner endpoint)
    {
        auto found = cells.find(key);
        if (found != cells.end())
            found->second->repairEndpoint(endpoint);
    }
};

SegmentedSpatialContext::SegmentedSpatialContext(float cellSize, SpatialPairPolicy pairPolicy)
    : impl(std::make_unique<Impl>(cellSize, pairPolicy))
{
}

SegmentedSpatialContext::~SegmentedSpatialContext()
{
    while (!impl->endpoints.empty())
        remove(impl->endpoints.begin()->first);
}

void SegmentedSpatialContext::insert(ProjectionOwner endpoint, BoundingRadiusProjectionAxis *xAxis,
                                     BoundingRadiusProjectionAxis *yAxis, const SpatialBounds &bounds)
{
    if (endpoint.pointer == nullptr || endpoint.kind != impl->pairPolicy.endpointKind || xAxis == nullptr ||
        yAxis == nullptr)
        throw std::invalid_argument("Segmented spatial endpoint does not match its context pair policy");
    if (impl->endpoints.find(endpoint) != impl->endpoints.end())
        throw std::logic_error("Segmented spatial endpoint is already registered");
    std::vector<GridCellKey> keys;
    impl->getKeys(bounds, keys);
    Impl::EndpointMembership membership{xAxis, yAxis, bounds, std::move(keys)};
    auto [stored, inserted] = impl->endpoints.emplace(endpoint, std::move(membership));
    (void)inserted;
    xAxis->getMin()->setProjectedPosition(bounds.minX);
    xAxis->getMax()->setProjectedPosition(bounds.maxX);
    yAxis->getMin()->setProjectedPosition(bounds.minY);
    yAxis->getMax()->setProjectedPosition(bounds.maxY);
    for (const auto key : stored->second.cells)
        impl->addToCell(key, endpoint, stored->second);
}

void SegmentedSpatialContext::update(ProjectionOwner endpoint, const SpatialBounds &bounds)
{
    auto found = impl->endpoints.find(endpoint);
    if (found == impl->endpoints.end())
        throw std::logic_error("Segmented spatial endpoint is not registered");
    auto &newKeys = impl->cellKeyScratch;
    impl->getKeys(bounds, newKeys);
    auto &membership = found->second;
    membership.xAxis->getMin()->setProjectedPosition(bounds.minX);
    membership.xAxis->getMax()->setProjectedPosition(bounds.maxX);
    membership.yAxis->getMin()->setProjectedPosition(bounds.minY);
    membership.yAxis->getMax()->setProjectedPosition(bounds.maxY);

    for (const auto key : newKeys)
        if (!std::binary_search(membership.cells.begin(), membership.cells.end(), key,
                                [](const GridCellKey &a, const GridCellKey &b) {
                                    return a.x < b.x || (a.x == b.x && a.y < b.y);
                                }))
            impl->addToCell(key, endpoint, membership);
    for (const auto key : newKeys)
        if (std::binary_search(membership.cells.begin(), membership.cells.end(), key,
                               [](const GridCellKey &a, const GridCellKey &b) {
                                   return a.x < b.x || (a.x == b.x && a.y < b.y);
                               }))
            impl->repairInCell(key, endpoint);
    for (const auto key : membership.cells)
        if (!std::binary_search(newKeys.begin(), newKeys.end(), key, [](const GridCellKey &a, const GridCellKey &b) {
                return a.x < b.x || (a.x == b.x && a.y < b.y);
            }))
            impl->removeFromCell(key, endpoint);
    membership.cells.swap(newKeys);
    newKeys.clear();
    membership.bounds = bounds;
}

void SegmentedSpatialContext::remove(ProjectionOwner endpoint)
{
    auto found = impl->endpoints.find(endpoint);
    if (found == impl->endpoints.end())
        return;
    for (const auto key : found->second.cells)
        impl->removeFromCell(key, endpoint);
    impl->endpoints.erase(found);
}

bool SegmentedSpatialContext::containsPair(ProjectionOwner endpointA, ProjectionOwner endpointB) const
{
    if (endpointA.kind != impl->pairPolicy.endpointKind || endpointB.kind != impl->pairPolicy.endpointKind)
        return false;
    return impl->pairWitnesses.find(AABBPair(endpointA, endpointB)) != impl->pairWitnesses.end();
}

size_t SegmentedSpatialContext::getCandidatePairCount() const
{
    return impl->pairWitnesses.size();
}

size_t SegmentedSpatialContext::getOwnedCapacityBytes() const
{
    size_t bytes = sizeof(*this) + sizeof(Impl) + impl->cells.bucket_count() * sizeof(void *) +
                   impl->cells.size() * sizeof(std::pair<const GridCellKey, std::unique_ptr<GridCell>>) +
                   impl->endpoints.bucket_count() * sizeof(void *) +
                   impl->endpoints.size() * sizeof(std::pair<const ProjectionOwner, Impl::EndpointMembership>) +
                   impl->pairWitnesses.bucket_count() * sizeof(void *) +
                   impl->pairWitnesses.size() * sizeof(std::pair<const AABBPair, size_t>) +
                   impl->cellKeyScratch.capacity() * sizeof(GridCellKey);
    bytes += (impl->cells.size() + impl->endpoints.size() + impl->pairWitnesses.size()) * 2 * sizeof(void *);
    for (const auto &[_, cell] : impl->cells)
        bytes += sizeof(GridCell) + cell->getOwnedCapacityBytes();
    for (const auto &[_, endpoint] : impl->endpoints)
        bytes += endpoint.cells.capacity() * sizeof(GridCellKey);
    return bytes;
}

size_t SegmentedSpatialContext::getCellCount() const
{
    return impl->cells.size();
}

size_t SegmentedSpatialContext::getCellMembershipCount(ProjectionOwner endpoint) const
{
    auto found = impl->endpoints.find(endpoint);
    return found == impl->endpoints.end() ? 0 : found->second.cells.size();
}

SpatialContext::SpatialContext(uint64_t runtimeSerial, SpatialDomain domain)
    : contextId(nextSpatialContextId.fetch_add(1, std::memory_order_relaxed)), runtimeSerialValue(runtimeSerial),
      contextDomain(domain)
{
    if (runtimeSerial == 0)
        throw std::invalid_argument("Spatial context requires a live runtime serial");
}

SpatialIdentity SpatialContext::identify(platformator::ObjectHandle endpoint) const
{
    if (!endpoint || endpoint.runtimeSerial != runtimeSerialValue)
        throw std::invalid_argument("Spatial endpoint does not belong to this runtime context");
    return {contextId, endpoint};
}

bool SpatialContext::owns(const SpatialIdentity &identity) const
{
    return identity.contextId == contextId && identity.endpoint.runtimeSerial == runtimeSerialValue &&
           static_cast<bool>(identity.endpoint);
}

uint64_t SpatialContext::id() const
{
    return contextId;
}

uint64_t SpatialContext::runtimeSerial() const
{
    return runtimeSerialValue;
}

SpatialDomain SpatialContext::domain() const
{
    return contextDomain;
}

struct PhysicsSpatialIndex::Impl
{
    struct ProxyRecord
    {
        size_t slot = SIZE_MAX;
        Collider *collider = nullptr;
        SpatialIdentity identity{};
        SpatialBounds exact{};
        SpatialBounds envelope{};
        DynamicAabbTree::ProxyId proxy = -1;
        uint64_t group = 0;
        uint64_t mask = 0;
        bool live = false;
    };

    struct Prepared
    {
        Collider *collider = nullptr;
        ProxyRecord *record = nullptr;
        bool shouldBeRegistered = false;
        bool geometryDirty = false;
        bool filterDirty = false;
        bool responseDirty = false;
        bool preparedReplay = false;
        SpatialBounds exact{};
        SpatialBounds envelope{};
    };

    struct ReplayInput
    {
        SpatialBounds exact{};
        uint64_t group = 0;
        uint64_t mask = 0;
        bool active = false;
    };

    struct QueryResult
    {
        ProxyRecord *source = nullptr;
        std::vector<ProxyRecord *> hits;
    };

    struct QueryContext
    {
        Impl *owner;
        std::vector<ProxyRecord *> *hits;
    };

    PhysicsManager *physics;
    PhysicsPairState *pairs;
    SpatialSettings settings;
    DynamicAabbTree tree;
    std::unique_ptr<SpatialContext> context;
    std::vector<std::unique_ptr<ProxyRecord>> recordStorage;
    std::vector<ProxyRecord *> recordSlots;
    std::vector<ProxyRecord *> freeRecords;
    std::unordered_map<Collider *, ProxyRecord *> recordsByCollider;
    std::vector<Collider *> pending;
    std::vector<Prepared> prepared;
    std::vector<Collider *> queryEndpoints;
    std::vector<QueryResult> queryResults;
    std::vector<std::pair<Collider *, Collider *>> affectedPairs;
    std::unordered_map<Collider *, ReplayInput> replayInputs;
    SpatialReplayIndexStats replayStats;
    bool preparedReplayMode = false;
    bool hasBuilt = false;

    Impl(PhysicsManager *physics, PhysicsPairState *pairs, const SpatialSettings &settings)
        : physics(physics), pairs(pairs), settings(settings), tree(), context(), recordStorage(), recordSlots(),
          freeRecords(), recordsByCollider(), pending(), prepared(), queryEndpoints(), queryResults(),
          affectedPairs(), replayInputs(), replayStats(), preparedReplayMode(false), hasBuilt(false)
    {
        SpatialResourceTagScope resourceTag(SpatialResourceTag::PhysicsIndex);
        if (pairs == nullptr)
            throw std::invalid_argument("Physics BVH requires its persistent pair state");
        if (!std::isfinite(settings.bvhPadding) || settings.bvhPadding < 0)
            throw std::invalid_argument("Physics BVH padding must be finite and nonnegative");
    }

    SpatialContext &getContext(Collider *collider)
    {
        const auto handle = collider->getHandle();
        if (!handle)
            throw std::logic_error("Physics BVH endpoint has no published generation identity");
        if (!context)
            context = std::make_unique<SpatialContext>(handle.runtimeSerial, SpatialDomain::Physics);
        return *context;
    }

    static bool collectQueryHit(DynamicAabbTree::ProxyId, uint64_t userData, void *context)
    {
        auto &query = *static_cast<QueryContext *>(context);
        if (userData >= query.owner->recordSlots.size())
            throw std::logic_error("Physics BVH query returned an invalid stable record slot");
        ProxyRecord *record = query.owner->recordSlots[static_cast<size_t>(userData)];
        if (record == nullptr || !record->live || record->collider == nullptr)
            throw std::logic_error("Physics BVH query returned a retired proxy record");
        query.hits->push_back(record);
        return true;
    }

    void enqueue(Collider *collider)
    {
        if (collider == nullptr || collider->broadPhaseQueueIndex != SIZE_MAX)
            return;
        collider->broadPhaseQueueIndex = pending.size();
        pending.push_back(collider);
    }

    void queueAdd(Collider *collider)
    {
        if (collider == nullptr)
            return;
        collider->desiredInBroadPhase = true;
        collider->dirty |= Collider::Geometry;
        enqueue(collider);
    }

    void queueRemove(Collider *collider)
    {
        if (collider == nullptr)
            return;
        collider->desiredInBroadPhase = false;
        enqueue(collider);
    }

    void queueSync(Collider *collider)
    {
        enqueue(collider);
    }

    void queuePreparedBounds(Collider *collider, const SpatialBounds &exact, uint64_t group, uint64_t mask,
                             bool active)
    {
        if (collider == nullptr || physics != nullptr)
            throw std::invalid_argument("Prepared bounds ingress is reserved for a headless replay index");
        if (!exact.isFiniteOrdered())
            throw std::invalid_argument("Prepared replay bounds must be finite and ordered");
        preparedReplayMode = true;
        replayInputs[collider] = {exact, group, mask, active};
        collider->collisionGroup = group;
        collider->collisionMask = mask;
        enqueue(collider);
    }

    SpatialBounds readBounds(Collider *collider) const
    {
        auto *x = collider->getXProjections();
        auto *y = collider->getYProjections();
        return {x->getMin()->getProjectedPosition(), y->getMin()->getProjectedPosition(),
                x->getMax()->getProjectedPosition(), y->getMax()->getProjectedPosition()};
    }

    bool endpointActive(Collider *collider) const
    {
        const auto replay = replayInputs.find(collider);
        if (replay != replayInputs.end())
            return replay->second.active;
        const auto *gameObject = collider->getGameObject();
        return gameObject != nullptr && gameObject->getActive();
    }

    void validateProxyBounds(Collider *collider, const SpatialBounds &exact, const SpatialBounds &envelope) const
    {
        if (!exact.isFiniteOrdered())
            throw std::invalid_argument("BVH physics endpoint " + std::to_string(collider->getHandle().slot) + ":" +
                                        std::to_string(collider->getHandle().generation) +
                                        " has invalid exact bounds");
        const double width = static_cast<double>(envelope.maxX) - envelope.minX;
        const double height = static_cast<double>(envelope.maxY) - envelope.minY;
        if (!envelope.isFiniteOrdered() || width >= 100000.0 || height >= 100000.0)
            throw std::invalid_argument("BVH physics endpoint " + std::to_string(collider->getHandle().slot) + ":" +
                                        std::to_string(collider->getHandle().generation) +
                                        " has unsupported padded proxy bounds or extent");
    }

    void prepareBatch()
    {
        PLATFORMATOR_BENCH_SCOPE(BoundsPrepare);
        prepared.clear();
        prepared.reserve(pending.size());
        for (Collider *collider : pending)
        {
            Prepared item;
            item.collider = collider;
            auto found = recordsByCollider.find(collider);
            item.record = found == recordsByCollider.end() ? nullptr : found->second;
            const auto replay = replayInputs.find(collider);
            if (replay != replayInputs.end())
            {
                item.preparedReplay = true;
                item.shouldBeRegistered = replay->second.active;
                item.geometryDirty = item.record == nullptr || item.record->exact.minX != replay->second.exact.minX ||
                                     item.record->exact.minY != replay->second.exact.minY ||
                                     item.record->exact.maxX != replay->second.exact.maxX ||
                                     item.record->exact.maxY != replay->second.exact.maxY;
                item.filterDirty = item.record == nullptr || item.record->group != replay->second.group ||
                                   item.record->mask != replay->second.mask;
                item.exact = replay->second.exact;
                item.envelope = item.exact.padded(settings.bvhPadding);
                if (item.shouldBeRegistered)
                    validateProxyBounds(collider, item.exact, item.envelope);
                prepared.push_back(item);
                continue;
            }
            item.shouldBeRegistered = collider->desiredInBroadPhase && !collider->isRetired() &&
                                      !collider->getGameObject()->getIsMarkedForDeletion() &&
                                      collider->getGameObject()->getActive();
            item.geometryDirty = (collider->dirty & Collider::Geometry) != 0;
            item.filterDirty = (collider->dirty & Collider::Filter) != 0;
            item.responseDirty = (collider->dirty & Collider::Response) != 0;
            if (item.shouldBeRegistered && (item.record == nullptr || item.geometryDirty))
            {
                collider->prepareGeometrySyncFromCache();
                item.exact = readBounds(collider);
                item.envelope = item.exact.padded(settings.bvhPadding);
                validateProxyBounds(collider, item.exact, item.envelope);
            }
            prepared.push_back(item);
        }
    }

    ProxyRecord *allocateRecord(Collider *collider, const Prepared &item)
    {
        ProxyRecord *record = nullptr;
        if (!freeRecords.empty())
        {
            record = freeRecords.back();
            freeRecords.pop_back();
        }
        else
        {
            auto value = std::make_unique<ProxyRecord>();
            record = value.get();
            record->slot = recordSlots.size();
            recordSlots.push_back(record);
            recordStorage.push_back(std::move(value));
        }
        record->collider = collider;
        record->identity = getContext(collider).identify(collider->getHandle());
        record->exact = item.exact;
        record->envelope = item.envelope;
        record->group = collider->getCollisionGroup();
        record->mask = collider->getCollisionMask();
        record->proxy = tree.addMarked({item.envelope.minX, item.envelope.minY, item.envelope.maxX,
                                        item.envelope.maxY},
                                       collider->getCollisionGroup(), record->slot);
        PLATFORMATOR_WORK_ADD(bvhTreeAdds, 1);
        record->live = true;
        recordSlots[record->slot] = record;
        recordsByCollider.emplace(collider, record);
        collider->setIsRegisteredInBroadPhase(true);
        collider->setIsRegisteredInGrid(false);
        if (preparedReplayMode)
            ++replayStats.proxyInserts;
        PLATFORMATOR_BENCH_ADD_COUNTER(ProxyInsertCount, 1);
        return record;
    }

    void retireRecord(ProxyRecord *record)
    {
        if (record == nullptr || !record->live)
            return;
        Collider *collider = record->collider;
        if (record->proxy >= 0)
        {
            tree.remove(record->proxy);
            PLATFORMATOR_WORK_ADD(bvhTreeRemoves, 1);
        }
        recordsByCollider.erase(collider);
        collider->setIsRegisteredInBroadPhase(false);
        collider->setIsRegisteredInGrid(false);
        recordSlots[record->slot] = nullptr;
        record->collider = nullptr;
        record->proxy = -1;
        record->live = false;
        record->identity = {};
        freeRecords.push_back(record);
        PLATFORMATOR_WORK_ADD(bvhRetiredRecords, 1);
        if (preparedReplayMode)
            ++replayStats.proxyRemoves;
        PLATFORMATOR_BENCH_ADD_COUNTER(ProxyRemoveCount, 1);
    }

    static bool contained(const SpatialBounds &outer, const SpatialBounds &inner)
    {
        return outer.minX <= inner.minX && outer.minY <= inner.minY && outer.maxX >= inner.maxX &&
               outer.maxY >= inner.maxY;
    }

    void applyMutations(bool &structuralChange)
    {
        PLATFORMATOR_BENCH_SCOPE(IndexUpdate);
        queryEndpoints.clear();
        for (Prepared &item : prepared)
        {
            Collider *collider = item.collider;
            ProxyRecord *record = item.record;
            if (!item.shouldBeRegistered)
            {
                if (record != nullptr)
                {
                    retireRecord(record);
                    structuralChange = true;
                }
                collider->setIsRegisteredInGrid(false);
                continue;
            }

            const bool newlyRegistered = record == nullptr;
            if (newlyRegistered || item.geometryDirty)
                PLATFORMATOR_BENCH_ADD_COUNTER(ChangedBoundsCount, 1);
            if (item.filterDirty)
                PLATFORMATOR_BENCH_ADD_COUNTER(FilterChangeCount, 1);
            if (record == nullptr)
            {
                record = allocateRecord(collider, item);
                item.record = record;
                structuralChange = true;
            }
            else
            {
                if (item.geometryDirty)
                {
                    record->exact = item.exact;
                    if (!contained(record->envelope, item.envelope))
                    {
                        tree.updateMarked(record->proxy,
                                          {item.envelope.minX, item.envelope.minY, item.envelope.maxX,
                                           item.envelope.maxY});
                        PLATFORMATOR_WORK_ADD(bvhTreeMoves, 1);
                        if (preparedReplayMode)
                            ++replayStats.proxyMoves;
                        PLATFORMATOR_BENCH_ADD_COUNTER(ProxyMoveCount, 1);
                        record->envelope = item.envelope;
                        structuralChange = true;
                    }
                    else
                    {
                        PLATFORMATOR_WORK_ADD(bvhContainedMoves, 1);
                        if (preparedReplayMode)
                            ++replayStats.envelopeReuse;
                        PLATFORMATOR_BENCH_ADD_COUNTER(EnvelopeReuseCount, 1);
                    }
                }
                if (item.filterDirty)
                    tree.setCategoryBits(record->proxy, collider->getCollisionGroup());
            }
            record->group = collider->getCollisionGroup();
            record->mask = collider->getCollisionMask();
            if (newlyRegistered || item.geometryDirty || item.filterDirty)
                queryEndpoints.push_back(collider);
            collider->setIsRegisteredInBroadPhase(true);
            collider->setIsRegisteredInGrid(false);
        }
    }

    void maintain(bool structuralChange)
    {
        PLATFORMATOR_BENCH_SCOPE(IndexMaintenance);
        const auto started = preparedReplayMode ? std::chrono::steady_clock::now()
                                                : std::chrono::steady_clock::time_point{};
        if (!hasBuilt && !recordsByCollider.empty())
        {
            const auto rebuilt = tree.rebuild(DynamicAabbTree::MaintenancePolicy::FullRebuild);
            PLATFORMATOR_WORK_ADD(bvhMaintenancePasses, 1);
            if (preparedReplayMode)
            {
                ++replayStats.rebuildCalls;
                replayStats.rebuildItems += static_cast<size_t>(std::max(0, rebuilt.rebuildItemCount));
            }
            PLATFORMATOR_BENCH_ADD_COUNTER(RebuildCallCount, 1);
            PLATFORMATOR_BENCH_ADD_COUNTER(RebuildItemCount, std::max(0, rebuilt.rebuildItemCount));
            hasBuilt = true;
        }
        else if (structuralChange && hasBuilt)
        {
            const auto policy = settings.bvhMaintenance == BvhMaintenance::NativePartial
                                    ? DynamicAabbTree::MaintenancePolicy::NativePartial
                                    : DynamicAabbTree::MaintenancePolicy::FullRebuild;
            const auto rebuilt = tree.rebuild(policy);
            PLATFORMATOR_WORK_ADD(bvhMaintenancePasses, 1);
            if (preparedReplayMode)
            {
                ++replayStats.rebuildCalls;
                replayStats.rebuildItems += static_cast<size_t>(std::max(0, rebuilt.rebuildItemCount));
            }
            PLATFORMATOR_BENCH_ADD_COUNTER(RebuildCallCount, 1);
            PLATFORMATOR_BENCH_ADD_COUNTER(RebuildItemCount, std::max(0, rebuilt.rebuildItemCount));
        }
        if (preparedReplayMode)
        {
            replayStats.maintenanceNanoseconds += static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started).count());
        }
    }

    void runQueries()
    {
        const auto queryStarted = std::chrono::steady_clock::now();
        {
        PLATFORMATOR_BENCH_SCOPE_IF(IndexQuery, !preparedReplayMode);
        std::sort(queryEndpoints.begin(), queryEndpoints.end(), [](Collider *a, Collider *b) {
            return a->getHandle() < b->getHandle();
        });
        queryEndpoints.erase(std::unique(queryEndpoints.begin(), queryEndpoints.end()), queryEndpoints.end());
        if (preparedReplayMode)
            replayStats.queries += queryEndpoints.size();
        PLATFORMATOR_BENCH_ADD_COUNTER(QueryCount, queryEndpoints.size());
        PLATFORMATOR_WORK_ADD(bvhQueryEndpoints, queryEndpoints.size());
        queryResults.resize(queryEndpoints.size());
        for (size_t i = 0; i < queryEndpoints.size(); ++i)
        {
            queryResults[i].source = recordsByCollider.at(queryEndpoints[i]);
            queryResults[i].hits.clear();
        }
        const auto queryOne = [&](size_t index) {
            PLATFORMATOR_BVH_QUERY_SCOPE(queryScope);
            QueryResult &result = queryResults[index];
            ProxyRecord *source = result.source;
            QueryContext context{this, &result.hits};
            tree.query({source->envelope.minX, source->envelope.minY, source->envelope.maxX,
                        source->envelope.maxY},
                       source->collider->getCollisionMask(), collectQueryHit, &context);
        };
        if (settings.bvhQueryExecution == BvhQueryExecution::Parallel && queryEndpoints.size() >= 2)
            tbb::parallel_for(size_t(0), queryEndpoints.size(), queryOne);
        else
            for (size_t i = 0; i < queryEndpoints.size(); ++i)
                queryOne(i);

        } // Immutable query workers have joined before reduction begins.
        if (preparedReplayMode)
            replayStats.queryNanoseconds = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - queryStarted).count());
        PLATFORMATOR_BENCH_SCOPE_IF(ResultReduce, !preparedReplayMode);
        affectedPairs.clear();
        const auto reduceStarted = preparedReplayMode ? std::chrono::steady_clock::now()
                                                      : std::chrono::steady_clock::time_point{};
        for (const QueryResult &result : queryResults)
        {
            Collider *source = result.source->collider;
            for (ProxyRecord *hit : result.hits)
            {
                if (hit != result.source)
                    PLATFORMATOR_BENCH_ADD_COUNTER(RawDiscoveryCount, 1);
                if (preparedReplayMode && hit != result.source)
                    ++replayStats.rawDiscoveries;
                if (hit == result.source || hit->collider == nullptr ||
                    (hit->collider->getCollisionGroup() & source->getCollisionMask()) == 0)
                    continue;
                Collider *a = source;
                Collider *b = hit->collider;
                if (b->getHandle() < a->getHandle())
                    std::swap(a, b);
                affectedPairs.emplace_back(a, b);
            }
        }

        // Geometry/filter changes also reconcile every old incident candidate, including pairs
        // that no longer appear in the current tree query.
        for (const Prepared &item : prepared)
        {
            if (!item.geometryDirty && !item.filterDirty && item.shouldBeRegistered)
                continue;
            const auto *adjacent = pairs->getTouchingPairs(item.collider);
            if (adjacent == nullptr)
                continue;
            for (const ColliderPair *pair : *adjacent)
            {
                Collider *a = pair->getObjectA();
                Collider *b = pair->getObjectB();
                if (b->getHandle() < a->getHandle())
                    std::swap(a, b);
                affectedPairs.emplace_back(a, b);
            }
        }
        std::sort(affectedPairs.begin(), affectedPairs.end(), [](const auto &a, const auto &b) {
            if (a.first->getHandle() != b.first->getHandle())
                return a.first->getHandle() < b.first->getHandle();
            return a.second->getHandle() < b.second->getHandle();
        });
        affectedPairs.erase(std::unique(affectedPairs.begin(), affectedPairs.end()), affectedPairs.end());

        for (const auto &[a, b] : affectedPairs)
        {
            PLATFORMATOR_WORK_ADD(bvhPairChecks, 1);
            const auto aRecord = recordsByCollider.find(a);
            const auto bRecord = recordsByCollider.find(b);
            const bool filterEligible = aRecord != recordsByCollider.end() && bRecord != recordsByCollider.end() &&
                                        endpointActive(a) && endpointActive(b) &&
                                        (a->getCollisionGroup() & b->getCollisionMask()) != 0 &&
                                        (b->getCollisionGroup() & a->getCollisionMask()) != 0;
            if (filterEligible)
                PLATFORMATOR_BENCH_ADD_COUNTER(ExactOverlapCount, 1);
            if (filterEligible && preparedReplayMode)
                ++replayStats.exactOverlaps;
            const bool eligible = filterEligible && aRecord->second->exact.overlapsInclusive(bRecord->second->exact);
            const bool existing = pairs->containsPair(a, b);
            if (eligible != existing)
            {
                pairs->applyPairWitnessDelta(a, b, eligible ? 1 : -1);
                if (preparedReplayMode)
                    eligible ? ++replayStats.pairAdds : ++replayStats.pairRemoves;
            }
        }
        // New pairs queue themselves. Refresh retained geometry/filter pairs once per dirty
        // endpoint, rather than walking a high-degree adjacency list once for every pair.
        for (const Prepared &item : prepared)
            if (item.shouldBeRegistered && (item.geometryDirty || item.filterDirty))
                pairs->queuePairsForCollider(item.collider);
        if (preparedReplayMode)
            replayStats.reduceNanoseconds += static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - reduceStarted).count());
        if (preparedReplayMode)
        {
            replayStats.treeHeight = static_cast<size_t>(std::max(0, tree.getMetrics().height));
            const size_t proxyCount = recordsByCollider.size();
            replayStats.treeNodes = proxyCount == 0 ? 0 : proxyCount * 2 - 1;
        }
    }

    void flush()
    {
        SpatialResourceTagScope resourceTag(SpatialResourceTag::PhysicsIndex);
        PLATFORMATOR_BENCH_SCOPE(SpatialUpdate);
        if (pending.empty())
        {
            PLATFORMATOR_BENCH_SET_COUNTER(SpatialProxyCount, recordsByCollider.size());
            PLATFORMATOR_BENCH_SET_COUNTER(TreeHeight, std::max(0, tree.getMetrics().height));
            PLATFORMATOR_BENCH_SET_COUNTER(TreeNodeCount,
                                          recordsByCollider.empty() ? 0 : recordsByCollider.size() * 2 - 1);
            if (preparedReplayMode)
            {
                replayStats.treeHeight = static_cast<size_t>(std::max(0, tree.getMetrics().height));
                const size_t proxyCount = recordsByCollider.size();
                replayStats.treeNodes = proxyCount == 0 ? 0 : proxyCount * 2 - 1;
            }
            return;
        }
        try
        {
            const auto updateStarted = std::chrono::steady_clock::now();
            prepareBatch(); // Validate every proxy before mutating the tree.
            bool structuralChange = false;
            applyMutations(structuralChange);
            if (preparedReplayMode)
                replayStats.updateNanoseconds = static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - updateStarted).count());
            maintain(structuralChange);
            runQueries();
            for (const Prepared &item : prepared)
            {
                if (item.responseDirty)
                    physics->refreshResponse(item.collider);
                item.collider->broadPhaseQueueIndex = SIZE_MAX;
                item.collider->dirty = 0;
            }
            PLATFORMATOR_BENCH_SET_COUNTER(SpatialProxyCount, recordsByCollider.size());
            PLATFORMATOR_BENCH_SET_COUNTER(TreeHeight, std::max(0, tree.getMetrics().height));
            PLATFORMATOR_BENCH_SET_COUNTER(TreeNodeCount,
                                          recordsByCollider.empty() ? 0 : recordsByCollider.size() * 2 - 1);
            pending.clear();
            prepared.clear();
        }
        catch (...)
        {
            // Keep the tree and pair store at a completed writer boundary. A caller may report the
            // fatal batch error; retirement can enqueue the same endpoint again during cleanup.
            for (Collider *collider : pending)
                collider->broadPhaseQueueIndex = SIZE_MAX;
            pending.clear();
            prepared.clear();
            queryEndpoints.clear();
            queryResults.clear();
            affectedPairs.clear();
            throw;
        }
    }
};

PhysicsSpatialIndex::PhysicsSpatialIndex(::PhysicsManager *owner, ::PhysicsPairState *pairState,
                                         const SpatialSettings &settings)
    : impl(std::make_unique<Impl>(owner, pairState, settings))
{
}

PhysicsSpatialIndex::~PhysicsSpatialIndex() = default;

void PhysicsSpatialIndex::queueAdd(Collider *collider)
{
    impl->queueAdd(collider);
}

void PhysicsSpatialIndex::queueRemove(Collider *collider)
{
    impl->queueRemove(collider);
}

void PhysicsSpatialIndex::queueSync(Collider *collider)
{
    impl->queueSync(collider);
}

void PhysicsSpatialIndex::flush()
{
    impl->flush();
}

void PhysicsSpatialIndex::queuePreparedBounds(Collider *collider, const SpatialBounds &exact, uint64_t group,
                                              uint64_t mask, bool active)
{
    impl->queuePreparedBounds(collider, exact, group, mask, active);
}

SpatialReplayIndexStats PhysicsSpatialIndex::flushPreparedBounds()
{
    impl->replayStats = {};
    impl->flush();
    return impl->replayStats;
}

SpatialReplayIndexStats PhysicsSpatialIndex::getPreparedBoundsStats() const
{
    auto stats = impl->replayStats;
    stats.treeHeight = static_cast<size_t>(std::max(0, impl->tree.getMetrics().height));
    const size_t proxyCount = impl->recordsByCollider.size();
    stats.treeNodes = proxyCount == 0 ? 0 : proxyCount * 2 - 1;
    return stats;
}

bool PhysicsSpatialIndex::contains(Collider *collider) const
{
    return impl->recordsByCollider.find(collider) != impl->recordsByCollider.end();
}

size_t PhysicsSpatialIndex::getProxyCount() const
{
    return impl->recordsByCollider.size();
}

size_t PhysicsSpatialIndex::getRecordCapacity() const
{
    return impl->recordStorage.size();
}

size_t PhysicsSpatialIndex::getCandidatePairCount() const
{
    return impl->pairs->getCandidatePairCount();
}

size_t PhysicsSpatialIndex::getOwnedCapacityBytes() const
{
    const auto metrics = impl->tree.getMetrics();
    size_t bytes = sizeof(*this) + sizeof(Impl) + metrics.memoryBytes + impl->recordStorage.capacity() * sizeof(std::unique_ptr<Impl::ProxyRecord>) +
           impl->recordSlots.capacity() * sizeof(Impl::ProxyRecord *) +
           impl->freeRecords.capacity() * sizeof(Impl::ProxyRecord *) +
           impl->pending.capacity() * sizeof(Collider *) + impl->prepared.capacity() * sizeof(Impl::Prepared) +
           impl->queryEndpoints.capacity() * sizeof(Collider *) +
           impl->queryResults.capacity() * sizeof(Impl::QueryResult) +
           impl->affectedPairs.capacity() * sizeof(std::pair<Collider *, Collider *>) +
           impl->recordsByCollider.bucket_count() * sizeof(void *) +
           impl->recordsByCollider.size() * sizeof(std::pair<Collider *const, Impl::ProxyRecord *>) +
           impl->replayInputs.bucket_count() * sizeof(void *) +
           impl->replayInputs.size() * sizeof(std::pair<Collider *const, Impl::ReplayInput>);
    bytes += (impl->recordsByCollider.size() + impl->replayInputs.size()) * 2 * sizeof(void *);
    for (const auto &record : impl->recordStorage) if (record) bytes += sizeof(Impl::ProxyRecord);
    for (const auto &query : impl->queryResults) bytes += query.hits.capacity() * sizeof(Impl::ProxyRecord *);
    return bytes;
}
} // namespace platformator_detail
