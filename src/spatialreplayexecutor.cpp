#include "spatialreplay.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <charconv>
#include <cmath>
#include <memory>
#include <limits>
#include <numbers>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include "dynamicaabbtree.h"
#include "grid.h"
#include "physicspairstate.h"
#include "platformator/boxcollider.h"
#include "platformator/camera.h"
#include "platformator/collider.h"
#include "platformator/sprite.h"
#include "renderculling.h"
#include "spatialindex.h"

namespace platformator_detail
{
namespace
{
using Json = nlohmann::json;

struct IdentityHash
{
    size_t operator()(const SpatialTraceIdentity &id) const noexcept
    {
        size_t hash = std::hash<uint64_t>{}(id.slot);
        hash ^= std::hash<uint64_t>{}(id.generation) + size_t(0x9e3779b9) + (hash << 6) + (hash >> 2);
        return hash;
    }
};

bool identityLess(const SpatialTraceIdentity &a, const SpatialTraceIdentity &b)
{
    return a.slot != b.slot ? a.slot < b.slot : a.generation < b.generation;
}

SpatialTracePair pairKey(SpatialTraceIdentity a, SpatialTraceIdentity b)
{
    if (identityLess(b, a))
        std::swap(a, b);
    return {a, b};
}

std::string pairLabel(const SpatialTracePair &pair)
{
    return std::to_string(pair.first.slot) + ":" + std::to_string(pair.first.generation) + "/" +
           std::to_string(pair.second.slot) + ":" + std::to_string(pair.second.generation);
}

uint64_t identityHash(const std::vector<std::string> &values)
{
    uint64_t hash = 14695981039346656037ULL;
    for (const auto &value : values)
    {
        for (const unsigned char byte : value)
        {
            hash ^= byte;
            hash *= 1099511628211ULL;
        }
        hash ^= 0xff;
        hash *= 1099511628211ULL;
    }
    return hash;
}

uint64_t elapsedNanoseconds(std::chrono::steady_clock::time_point started)
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started).count());
}

uint64_t decimal(const Json &value)
{
    if (!value.is_string())
        throw std::runtime_error("Spatial replay operation lost its validated uint64 string");
    const std::string text = value.get<std::string>();
    uint64_t result = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        throw std::runtime_error("Spatial replay operation has invalid uint64 identity data");
    return result;
}

SpatialTraceIdentity readId(const Json &value)
{
    return {decimal(value.at("slot")), decimal(value.at("generation"))};
}

std::array<double, 4> readBounds(const Json &value)
{
    return {value.at(0).get<double>(), value.at(1).get<double>(), value.at(2).get<double>(),
            value.at(3).get<double>()};
}

uint64_t readBits(const Json &value)
{
    return decimal(value);
}

bool readBool(const Json &value)
{
    return value.get<bool>();
}

struct Endpoint
{
    SpatialTraceIdentity id;
    SpatialBounds exact;
    SpatialBounds envelope;
    uint64_t group = 0;
    uint64_t mask = 0;
    bool active = false;
    bool indexed = false;
    bool geometryDirty = false;
    bool filterDirty = false;
    uint32_t pairHandleSlot = 0;
    std::unique_ptr<BoxCollider> pairEndpoint;
    std::unique_ptr<BoundingRadiusProjectionAxis> xAxis;
    std::unique_ptr<BoundingRadiusProjectionAxis> yAxis;
};

class CollisionReplay
{
  public:
    CollisionReplay(const SpatialTrace &trace, const SpatialSettings &settings, bool validate,
                    SpatialReplayResourceObserver resourceObserver, void *resourceObserverContext)
        : trace(trace), settings(settings), validate(validate), counters(nullptr), segmented(), indexedCount(0),
          endpointSetupNanoseconds(0), pairState(nullptr), bvhIndex(), resourceObserver(resourceObserver),
          resourceObserverContext(resourceObserverContext)
    {
        if (trace.header.domain != SpatialTraceDomain::Collision)
            throw std::invalid_argument("Expected collision replay trace");
        const auto endpointSetupStarted = std::chrono::steady_clock::now();
        size_t recordCapacity = trace.collisionInitial.size();
        size_t peakRecordCapacity = recordCapacity;
        for (const auto &step : trace.steps)
        {
            size_t additions = 0, removals = 0;
            for (const Json &operation : step.operations)
            {
                const auto name = operation.at("op").get<std::string>();
                if (name == "add")
                    ++additions;
                else if (name == "remove")
                    ++removals;
            }
            if (removals > recordCapacity + additions)
                throw std::logic_error("Validated collision trace retired more records than it created");
            peakRecordCapacity = std::max(peakRecordCapacity, recordCapacity + additions);
            recordCapacity = recordCapacity + additions - removals;
        }
        if (peakRecordCapacity > std::numeric_limits<uint32_t>::max())
            throw std::overflow_error("Collision replay endpoint handle capacity exceeded");
        storage.reserve(peakRecordCapacity);
        pairEndpoints.reserve(peakRecordCapacity);
        freeRecords.reserve(peakRecordCapacity);
        for (size_t slot = 0; slot < peakRecordCapacity; ++slot)
        {
            auto endpoint = std::make_unique<Endpoint>();
            endpoint->pairHandleSlot = static_cast<uint32_t>(slot);
            endpoint->pairEndpoint = std::make_unique<BoxCollider>();
            freeRecords.push_back(endpoint.get());
            pairEndpoints.emplace(endpoint->pairEndpoint.get(), endpoint.get());
            storage.push_back(std::move(endpoint));
        }
        if (settings.broadPhase == BroadPhaseMode::Segmented)
        {
            segmented = std::make_unique<Grid>(nullptr, &pairState);
        }
        else
            bvhIndex = std::make_unique<PhysicsSpatialIndex>(nullptr, &pairState, settings);
        endpointSetupNanoseconds = elapsedNanoseconds(endpointSetupStarted);
    }

    ~CollisionReplay()
    {
        // run() may have exited through an exception from publication, validation, or the
        // resource observer. A counter pointer is only valid within its stack-bound scope.
        counters = nullptr;
        for (const auto &[_, endpoint] : live)
            if (endpoint->indexed)
            {
                try
                {
                    removeIndex(*endpoint);
                }
                catch (...)
                {
                    // A failed batch is already being reported. Continue draining the owner
                    // without replacing that diagnostic or throwing from this destructor.
                }
            }
        try
        {
            maintain();
        }
        catch (...)
        {
            // The indexes are destroyed below while every endpoint and axis is still alive.
        }

        // Segmented cells retain endpoint/axis addresses, and the BVH adapter retains collider
        // addresses. Destroy both owners before their stable replay endpoints.
        segmented.reset();
        bvhIndex.reset();
        for (const auto &endpoint : storage)
        {
            endpoint->indexed = false;
            endpoint->xAxis.reset();
            endpoint->yAxis.reset();
        }
        for (const auto &endpoint : storage)
        {
            try
            {
                removeIncident(*endpoint);
            }
            catch (...)
            {
                // PhysicsPairState is destroyed before storage by member order, so even an
                // allocation failure here cannot leave a pair reference past endpoint lifetime.
            }
        }
    }

    SpatialReplayRun run()
    {
        SpatialReplayRun result;
        SpatialReplayCounters initial;
        const auto initialStart = std::chrono::steady_clock::now();
        {
            CounterBinding binding(*this, initial);
            auto indexUpdateStart = std::chrono::steady_clock::now();
            for (const auto &source : trace.collisionInitial)
            {
                Endpoint *endpoint = allocate(source.id, {source.minX, source.minY, source.maxX, source.maxY},
                                               source.group, source.mask, source.active);
                if (endpoint->active)
                    addIndex(*endpoint);
            }
            initial.indexUpdateNanoseconds += elapsedNanoseconds(indexUpdateStart);
            maintain();
        }
        result.initializationNanoseconds = endpointSetupNanoseconds + static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - initialStart).count());
        gauges(initial);
        result.initialCounters = initial;
        observeOwnedCapacity(true);
        std::vector<std::string> previousValidated;
        if (validate)
        {
            result.initialOutput = validateAndSnapshot();
            previousValidated = result.initialOutput;
        }
        result.initialOutputHash = validate ? identityHash(result.initialOutput) : 0;

        result.measuredSamples.reserve(trace.header.measureSteps);
        for (size_t index = 0; index < trace.steps.size(); ++index)
        {
            SpatialReplayCounters current;
            const auto started = std::chrono::steady_clock::now();
            {
                CounterBinding binding(*this, current);
                apply(trace.steps[index]);
                gauges(current);
            }
            const auto ended = std::chrono::steady_clock::now();
            std::vector<std::string> validatedOutput;
            if (validate)
            {
                validatedOutput = validateAndSnapshot();
                validateTransition(previousValidated, validatedOutput, current, index);
                previousValidated = validatedOutput;
            }
            observeOwnedCapacity(false);
            if (index < trace.header.warmupSteps)
                continue;
            SpatialReplaySample sample;
            sample.durationNanoseconds = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(ended - started).count());
            sample.counters = current;
            if (validate)
                sample.output = std::move(validatedOutput);
            sample.outputHash = validate ? identityHash(sample.output) : 0;
            result.measuredSamples.push_back(std::move(sample));
        }
        return result;
    }

  private:
    class CounterBinding
    {
      public:
        CounterBinding(CollisionReplay &replay, SpatialReplayCounters &value)
            : replay(replay), previous(std::exchange(replay.counters, &value))
        {
        }

        ~CounterBinding() { replay.counters = previous; }

        CounterBinding(const CounterBinding &) = delete;
        CounterBinding &operator=(const CounterBinding &) = delete;

      private:
        CollisionReplay &replay;
        SpatialReplayCounters *previous;
    };

    void observeOwnedCapacity(bool initial) const
    {
        if (resourceObserver == nullptr)
            return;
        const SpatialReplayOwnedCapacitySnapshot snapshot{
            bvhIndex ? bvhIndex->getOwnedCapacityBytes() : segmented ? segmented->getOwnedCapacityBytes() : 0,
            pairState.getOwnedCapacityBytes(), 0, pairState.getScratchCapacityBytes()};
        resourceObserver(snapshot, initial, resourceObserverContext);
    }

    static bool eligiblePair(void *context, ProjectionOwner first, ProjectionOwner second)
    {
        auto &self = *static_cast<CollisionReplay *>(context);
        auto &a = *static_cast<Endpoint *>(first.pointer);
        auto &b = *static_cast<Endpoint *>(second.pointer);
        if (self.counters)
            ++self.counters->rawDiscoveries;
        const bool filterEligible = a.active && b.active && (a.group & b.mask) != 0 && (b.group & a.mask) != 0;
        if (filterEligible && self.counters)
            if (self.exactSeen.insert(pairKey(a.id, b.id)).second)
                ++self.counters->exactOverlaps;
        return filterEligible && a.exact.overlapsInclusive(b.exact);
    }

    static void pairChanged(void *context, ProjectionOwner first, ProjectionOwner second, int delta)
    {
        auto &self = *static_cast<CollisionReplay *>(context);
        self.setPair(*static_cast<Endpoint *>(first.pointer), *static_cast<Endpoint *>(second.pointer), delta > 0);
    }

    Endpoint *allocate(SpatialTraceIdentity id, SpatialBounds bounds, uint64_t group, uint64_t mask, bool active)
    {
        if (freeRecords.empty())
            throw std::logic_error("Collision replay exhausted its preallocated endpoint records");
        Endpoint *endpoint = freeRecords.back();
        freeRecords.pop_back();
        pairState.setReplayEndpointIdentity(endpoint->pairEndpoint.get(),
                                            {endpoint->pairHandleSlot, id.generation, 1});
        endpoint->id = id;
        endpoint->exact = bounds;
        endpoint->group = group;
        endpoint->mask = mask;
        endpoint->active = active;
        endpoint->indexed = false;
        endpoint->geometryDirty = false;
        endpoint->filterDirty = false;
        endpoint->xAxis.reset();
        endpoint->yAxis.reset();
        live[endpoint->id] = endpoint;
        return endpoint;
    }

    Endpoint *find(const Json &idValue)
    {
        auto found = live.find(readId(idValue));
        if (found == live.end())
            throw std::logic_error("Validated replay operation has no live endpoint");
        return found->second;
    }

    void addIndex(Endpoint &endpoint)
    {
        endpoint.envelope = settings.broadPhase == BroadPhaseMode::DynamicBvh
                                ? endpoint.exact.padded(settings.bvhPadding)
                                : endpoint.exact;
        if (settings.broadPhase == BroadPhaseMode::DynamicBvh)
        {
            bvhIndex->queuePreparedBounds(endpoint.pairEndpoint.get(), endpoint.exact, endpoint.group, endpoint.mask,
                                          true);
        }
        else
        {
            segmented->queuePreparedBounds(endpoint.pairEndpoint.get(), endpoint.exact, endpoint.group, endpoint.mask, true);
        }
        endpoint.indexed = true;
        ++indexedCount;

    }

    void setPair(Endpoint &a, Endpoint &b, bool enabled)
    {
        Collider *colliderA = a.pairEndpoint.get(), *colliderB = b.pairEndpoint.get();
        const bool exists = pairState.containsPair(colliderA, colliderB);
        if (exists == enabled)
            return;
        pairState.applyPairWitnessDelta(colliderA, colliderB, enabled ? 1 : -1);
        if (counters)
            enabled ? ++counters->pairAdds : ++counters->pairRemoves;
    }

    void removeIncident(Endpoint &endpoint)
    {
        const auto *touching = pairState.getTouchingPairs(endpoint.pairEndpoint.get());
        if (touching == nullptr)
            return;
        std::vector<std::pair<Collider *, Collider *>> old;
        old.reserve(touching->size());
        for (const ColliderPair *pair : *touching)
            old.emplace_back(pair->getObjectA(), pair->getObjectB());
        for (const auto &[first, second] : old)
        {
            pairState.applyPairWitnessDelta(first, second, -1);
            if (counters)
                ++counters->pairRemoves;
        }
    }

    void removeIndex(Endpoint &endpoint)
    {
        if (!endpoint.indexed)
            return;
        if (settings.broadPhase == BroadPhaseMode::DynamicBvh)
        {
            bvhIndex->queuePreparedBounds(endpoint.pairEndpoint.get(), endpoint.exact, endpoint.group, endpoint.mask,
                                          false);
        }
        else
            segmented->queuePreparedBounds(endpoint.pairEndpoint.get(), endpoint.exact, endpoint.group, endpoint.mask, false);
        endpoint.indexed = false;
        if (indexedCount == 0)
            throw std::logic_error("Collision replay indexed proxy count underflow");
        --indexedCount;
        endpoint.xAxis.reset();
        endpoint.yAxis.reset();

    }

    void updateIndex(Endpoint &endpoint)
    {
        const SpatialBounds next = settings.broadPhase == BroadPhaseMode::DynamicBvh
                                       ? endpoint.exact.padded(settings.bvhPadding)
                                       : endpoint.exact;
        if (settings.broadPhase == BroadPhaseMode::DynamicBvh)
        {
            bvhIndex->queuePreparedBounds(endpoint.pairEndpoint.get(), endpoint.exact, endpoint.group, endpoint.mask,
                                          true);
        }
        else
        {
            segmented->queuePreparedBounds(endpoint.pairEndpoint.get(), endpoint.exact, endpoint.group, endpoint.mask, true);
            endpoint.envelope = next;
        }
    }

    void maintain()
    {
        const SpatialReplayIndexStats stats = bvhIndex ? bvhIndex->flushPreparedBounds() : segmented->flushPreparedBounds();
        if (counters)
        {
            counters->proxyInserts += stats.proxyInserts;
            counters->proxyRemoves += stats.proxyRemoves;
            counters->proxyMoves += stats.proxyMoves;
            counters->envelopeReuse += stats.envelopeReuse;
            counters->queries += stats.queries;
            counters->rawDiscoveries += stats.rawDiscoveries;
            counters->exactOverlaps += stats.exactOverlaps;
            counters->pairAdds += stats.pairAdds;
            counters->pairRemoves += stats.pairRemoves;
            counters->rebuildCalls += stats.rebuildCalls;
            counters->rebuildItems += stats.rebuildItems;
            counters->indexMaintenanceNanoseconds += stats.maintenanceNanoseconds;
            counters->indexQueryNanoseconds += stats.queryNanoseconds;
            counters->resultReduceNanoseconds += stats.reduceNanoseconds;
            counters->indexUpdateNanoseconds += stats.updateNanoseconds;
        }
    }

    void apply(const SpatialTraceStep &step)
    {
        exactSeen.clear();
        std::unordered_set<Endpoint *> dirty, moved, filtered;
        std::vector<Endpoint *> removed;
        for (const Json &op : step.operations)
        {
            const std::string name = op.at("op").get<std::string>();
            if (name == "add")
            {
                const auto &source = op.at("collider");
                const auto values = readBounds(source.at("bounds"));
                Endpoint *endpoint = allocate(readId(source.at("id")),
                                              {static_cast<float>(values[0]), static_cast<float>(values[1]),
                                               static_cast<float>(values[2]), static_cast<float>(values[3])},
                                              readBits(source.at("group")), readBits(source.at("mask")),
                                              readBool(source.at("active")));
                dirty.insert(endpoint);
                moved.insert(endpoint);
                continue;
            }
            Endpoint *endpoint = find(op.at("id"));
            dirty.insert(endpoint);
            if (name == "remove")
            {
                endpoint->active = false;
                removed.push_back(endpoint);
            }
            else if (name == "update_bounds")
            {
                const auto values = readBounds(op.at("bounds"));
                const SpatialBounds next{static_cast<float>(values[0]), static_cast<float>(values[1]),
                                         static_cast<float>(values[2]), static_cast<float>(values[3])};
                if (endpoint->exact.minX != next.minX || endpoint->exact.minY != next.minY ||
                    endpoint->exact.maxX != next.maxX || endpoint->exact.maxY != next.maxY)
                {
                    endpoint->exact = next;
                    endpoint->geometryDirty = true;
                    moved.insert(endpoint);
                }
            }
            else if (name == "set_filter")
            {
                const uint64_t group = readBits(op.at("group")), mask = readBits(op.at("mask"));
                if (endpoint->group != group || endpoint->mask != mask)
                {
                    endpoint->group = group;
                    endpoint->mask = mask;
                    endpoint->filterDirty = true;
                    filtered.insert(endpoint);
                }
            }
            else if (name == "set_active")
                endpoint->active = readBool(op.at("active"));
        }
        counters->changedBounds = moved.size();
        counters->filterChanges = filtered.size();
        std::vector<Endpoint *> orderedDirty(dirty.begin(), dirty.end());
        std::sort(orderedDirty.begin(), orderedDirty.end(), [](const Endpoint *a, const Endpoint *b) {
            return identityLess(a->id, b->id);
        });
        const auto indexUpdateStarted = std::chrono::steady_clock::now();
        for (Endpoint *endpoint : orderedDirty)
        {
            if (endpoint->indexed && !endpoint->active)
                removeIndex(*endpoint);
            else if (!endpoint->indexed && endpoint->active)
            {
                addIndex(*endpoint);
            }
            else if (endpoint->indexed && endpoint->active && (endpoint->geometryDirty || endpoint->filterDirty))
                updateIndex(*endpoint);
            endpoint->geometryDirty = false;
            endpoint->filterDirty = false;
        }
        counters->indexUpdateNanoseconds += elapsedNanoseconds(indexUpdateStarted);
        maintain();
        counters->candidatePairCount = pairState.getCandidatePairCount();
        for (Endpoint *endpoint : removed)
        {
            live.erase(endpoint->id);
            freeRecords.push_back(endpoint);
        }
    }

    void gauges(SpatialReplayCounters &value)
    {
        value.candidatePairCount = pairState.getCandidatePairCount();
        if (settings.broadPhase == BroadPhaseMode::DynamicBvh)
        {
            const auto stats = bvhIndex->getPreparedBoundsStats();
            value.proxyCount = bvhIndex->getProxyCount();
            value.treeHeight = stats.treeHeight;
            value.treeNodes = stats.treeNodes;
        }
        else
            value.proxyCount = indexedCount;
    }

    std::vector<std::string> validateAndSnapshot()
    {
        std::vector<Endpoint *> endpoints;
        for (const auto &[_, endpoint] : live)
            if (endpoint->active)
                endpoints.push_back(endpoint);
        std::sort(endpoints.begin(), endpoints.end(), [](const Endpoint *a, const Endpoint *b) {
            return identityLess(a->id, b->id);
        });
        std::set<SpatialTracePair> expected;
        const auto examine = [&](const Endpoint &a, const Endpoint &b) {
            if ((a.exact.minX <= b.exact.maxX && b.exact.minX <= a.exact.maxX &&
                 a.exact.minY <= b.exact.maxY && b.exact.minY <= a.exact.maxY) && (a.group & b.mask) != 0 && (b.group & a.mask) != 0)
                expected.insert(pairKey(a.id, b.id));
        };
        if (endpoints.size() <= 512)
        {
            for (size_t i = 0; i < endpoints.size(); ++i)
                for (size_t j = i + 1; j < endpoints.size(); ++j)
                    examine(*endpoints[i], *endpoints[j]);
        }
        else
        {
            std::sort(endpoints.begin(), endpoints.end(), [](const Endpoint *a, const Endpoint *b) {
                return a->exact.minX != b->exact.minX ? a->exact.minX < b->exact.minX
                                                       : identityLess(a->id, b->id);
            });
            for (size_t i = 0; i < endpoints.size(); ++i)
            {
                const Endpoint &first = *endpoints[i];
                for (size_t j = i + 1; j < endpoints.size() && endpoints[j]->exact.minX <= first.exact.maxX; ++j)
                    examine(first, *endpoints[j]);
            }
        }
        std::set<SpatialTracePair> actual;
        for (const auto &[_, endpoint] : live)
        {
            const auto *touching = pairState.getTouchingPairs(endpoint->pairEndpoint.get());
            if (touching == nullptr)
                continue;
            for (const ColliderPair *pair : *touching)
            {
                const auto first = pairEndpoints.find(pair->getObjectA());
                const auto second = pairEndpoints.find(pair->getObjectB());
                if (first == pairEndpoints.end() || second == pairEndpoints.end())
                    throw std::logic_error("Collision replay pair state references an unknown endpoint");
                actual.insert(pairKey(first->second->id, second->second->id));
            }
        }
        if (expected != actual)
            throw std::runtime_error("Collision replay candidate set differs from the independent O(N^2) reference");
        std::vector<std::string> output;
        output.reserve(actual.size());
        for (const auto &pair : actual)
            output.push_back(pairLabel(pair));
        return output;
    }

    static void validateTransition(const std::vector<std::string> &previous, const std::vector<std::string> &current,
                                   const SpatialReplayCounters &value, size_t stepIndex)
    {
        const std::set<std::string> prior(previous.begin(), previous.end());
        const std::set<std::string> next(current.begin(), current.end());
        size_t additions = 0, removals = 0;
        for (const auto &pair : next) additions += !prior.contains(pair);
        for (const auto &pair : prior) removals += !next.contains(pair);
        // Publication reports logical net transitions after all endpoint/cell changes reconcile.
        if (value.pairAdds != additions || value.pairRemoves != removals)
            throw std::runtime_error("Collision replay incremental pair transitions differ from the independent reference at step " +
                                     std::to_string(stepIndex) + ": expected +" + std::to_string(additions) + "/-" +
                                     std::to_string(removals) + ", observed +" + std::to_string(value.pairAdds) + "/-" +
                                     std::to_string(value.pairRemoves));
    }

    const SpatialTrace &trace;
    SpatialSettings settings;
    bool validate;
    SpatialReplayCounters *counters;
    std::unique_ptr<Grid> segmented;
    size_t indexedCount;
    uint64_t endpointSetupNanoseconds;
    std::vector<std::unique_ptr<Endpoint>> storage;
    std::unordered_map<Collider *, Endpoint *> pairEndpoints;
    PhysicsPairState pairState;
    std::unique_ptr<PhysicsSpatialIndex> bvhIndex;
    SpatialReplayResourceObserver resourceObserver;
    void *resourceObserverContext;
    std::vector<Endpoint *> freeRecords;
    std::unordered_map<SpatialTraceIdentity, Endpoint *, IdentityHash> live;
    std::set<SpatialTracePair> exactSeen;
};

struct VisibilityEndpoint
{
    SpatialTraceIdentity id;
    bool active = false;
    bool uncullable = false;
    double centerX = 0.0, centerY = 0.0, width = 0.0, height = 0.0;
    double scaleX = 1.0, scaleY = 1.0, angle = 0.0;
    size_t orderIndex = 0;
    std::unique_ptr<Sprite> spriteToken;
};

class VisibilityReplay
{
  public:
    VisibilityReplay(const SpatialTrace &trace, const SpatialSettings &settings, bool validate,
                     SpatialReplayResourceObserver resourceObserver, void *resourceObserverContext)
        : trace(trace), settings(settings), validate(validate), counters(nullptr), storage(), freeRecords(), sprites(),
          endpointBySprite(), order(), snapshotDrawOrder(), result(), cameraIdentity(), culling(nullptr, settings, 0),
          view(), pendingRetired(), endpointSetupNanoseconds(0), geometryRevision(0), viewRevision(0), orderRevision(0),
          resourceObserver(resourceObserver), resourceObserverContext(resourceObserverContext)
    {
        if (trace.header.domain != SpatialTraceDomain::Visibility)
            throw std::invalid_argument("Expected visibility replay trace");
        if (!std::isfinite(settings.bvhPadding) || settings.bvhPadding < 0.0f)
            throw std::invalid_argument("Visibility replay padding must be finite and nonnegative");
        const auto setupStarted = std::chrono::steady_clock::now();
        size_t liveRecords = trace.visibilityInitial.size();
        size_t peakRecords = liveRecords;
        for (const auto &step : trace.steps)
        {
            size_t additions = 0, removals = 0;
            for (const Json &operation : step.operations)
            {
                const auto name = operation.at("op").get<std::string>();
                if (name == "add")
                    ++additions;
                else if (name == "remove")
                    ++removals;
            }
            if (removals > liveRecords + additions)
                throw std::logic_error("Validated visibility trace retired more records than it created");
            peakRecords = std::max(peakRecords, liveRecords + additions);
            liveRecords = liveRecords + additions - removals;
        }
        storage.reserve(peakRecords);
        freeRecords.reserve(peakRecords);
        sprites.reserve(peakRecords);
        endpointBySprite.reserve(peakRecords);
        for (size_t index = 0; index < peakRecords; ++index)
        {
            auto endpoint = std::make_unique<VisibilityEndpoint>();
            endpoint->spriteToken = std::make_unique<Sprite>();
            freeRecords.push_back(endpoint.get());
            endpointBySprite.emplace(endpoint->spriteToken.get(), endpoint.get());
            storage.push_back(std::move(endpoint));
        }
        endpointSetupNanoseconds = elapsedNanoseconds(setupStarted);
    }

    SpatialReplayRun run()
    {
        SpatialReplayRun output;
        SpatialReplayCounters initial;
        counters = &initial;
        const auto initializationStarted = std::chrono::steady_clock::now();
        view = trace.visibilityView;
        for (const auto &source : trace.visibilityInitial)
        {
            VisibilityEndpoint *sprite = allocateSprite(source);
            if (sprite->active)
                orderAdd(*sprite);
        }
        const auto &initialOutput = refreshResult();
        output.initializationNanoseconds =
            endpointSetupNanoseconds + elapsedNanoseconds(initializationStarted);
        gauges(initial);
        output.initialCounters = initial;
        observeOwnedCapacity(true);
        if (validate)
        {
            validateVisibility(initialOutput);
            output.initialOutput = labels(initialOutput);
        }
        output.initialOutputHash = validate ? identityHash(output.initialOutput) : 0;

        output.measuredSamples.reserve(trace.header.measureSteps);
        for (size_t index = 0; index < trace.steps.size(); ++index)
        {
            SpatialReplayCounters current;
            counters = &current;
            const auto started = std::chrono::steady_clock::now();
            apply(trace.steps[index]);
            const auto &visible = refreshResult();
            releaseRetired();
            gauges(current);
            const auto ended = std::chrono::steady_clock::now();
            if (validate)
                validateVisibility(visible);
            observeOwnedCapacity(false);
            if (index < trace.header.warmupSteps)
                continue;
            SpatialReplaySample sample;
            sample.durationNanoseconds = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(ended - started).count());
            sample.counters = current;
            if (validate)
                sample.output = labels(visible);
            sample.outputHash = validate ? identityHash(sample.output) : 0;
            output.measuredSamples.push_back(std::move(sample));
        }
        counters = nullptr;
        return output;
    }

  private:
    void observeOwnedCapacity(bool initial) const
    {
        if (resourceObserver == nullptr)
            return;
        const SpatialReplayOwnedCapacitySnapshot snapshot{0, 0, culling.getOwnedCapacityBytes(), 0};
        resourceObserver(snapshot, initial, resourceObserverContext);
    }

    VisibilityEndpoint *allocateSprite(const VisibilityTraceSprite &source)
    {
        if (freeRecords.empty())
            throw std::logic_error("Visibility replay exhausted its preallocated endpoint records");
        VisibilityEndpoint *sprite = freeRecords.back();
        freeRecords.pop_back();
        sprite->id = source.id;
        sprite->active = source.active;
        sprite->uncullable = false;
        sprite->centerX = source.centerX;
        sprite->centerY = source.centerY;
        sprite->width = source.width;
        sprite->height = source.height;
        sprite->scaleX = source.scaleX;
        sprite->scaleY = source.scaleY;
        sprite->angle = source.angleDegrees;
        sprites.emplace(sprite->id, sprite);
        return sprite;
    }

    VisibilityEndpoint *createSprite(const Json &value)
    {
        VisibilityTraceSprite parsed;
        parsed.id = readId(value.at("id"));
        parsed.centerX = value.at("center").at(0).get<double>();
        parsed.centerY = value.at("center").at(1).get<double>();
        parsed.width = value.at("dimensions").at(0).get<double>();
        parsed.height = value.at("dimensions").at(1).get<double>();
        parsed.scaleX = value.at("scale").at(0).get<double>();
        parsed.scaleY = value.at("scale").at(1).get<double>();
        parsed.angleDegrees = value.at("angle_degrees").get<double>();
        parsed.active = value.at("active").get<bool>();
        auto *sprite = allocateSprite(parsed);
        if (sprite->active)
            orderAdd(*sprite);
        return sprite;
    }

    void publishSprite(VisibilityEndpoint &sprite)
    {
        culling.updateSnapshotSprite({sprite.spriteToken.get(), sprite.centerX, sprite.centerY, sprite.width,
            sprite.height, sprite.scaleX, sprite.scaleY, sprite.angle});
    }

    void orderAdd(VisibilityEndpoint &sprite)
    {
        sprite.orderIndex = order.size();
        order.push_back(&sprite);
        sprite.spriteToken->setWindowIndex(sprite.orderIndex);
        sprite.spriteToken->setIsRegisteredInWindow(true);
        snapshotDrawOrder.push_back(sprite.spriteToken.get());
        publishSprite(sprite);
        ++orderRevision;
    }

    void orderRemove(VisibilityEndpoint &sprite)
    {
        const size_t index = sprite.orderIndex;
        if (index >= order.size() || order[index] != &sprite)
            throw std::logic_error("Visibility replay draw-order slot is stale");
        VisibilityEndpoint *last = order.back();
        order[index] = last;
        last->orderIndex = index;
        order.pop_back();
        snapshotDrawOrder[index] = snapshotDrawOrder.back();
        snapshotDrawOrder.pop_back();
        last->spriteToken->setWindowIndex(index);
        sprite.spriteToken->setIsRegisteredInWindow(false);
        culling.removeSprite(sprite.spriteToken.get());
        ++orderRevision;
    }

    static bool sameView(const VisibilityTraceView &a, const VisibilityTraceView &b)
    {
        return a.id == b.id && a.positionX == b.positionX && a.positionY == b.positionY && a.width == b.width &&
               a.height == b.height && a.outputWidth == b.outputWidth && a.outputHeight == b.outputHeight;
    }

    void apply(const SpatialTraceStep &step)
    {
        const VisibilityTraceView oldView = view;
        size_t geometryChanges = 0;
        pendingRetired.clear();
        for (const Json &op : step.operations)
        {
            const std::string name = op.at("op").get<std::string>();
            if (name == "add")
            {
                createSprite(op.at("sprite"));
                ++geometryChanges;
            }
            else if (name == "remove")
            {
                const auto id = readId(op.at("id"));
                auto found = sprites.find(id);
                if (found == sprites.end())
                    throw std::logic_error("Validated visibility removal has no live sprite");
                auto *sprite = found->second;
                if (sprite->active)
                {
                    orderRemove(*sprite);
                    sprite->active = false;
                }
                sprites.erase(found);
                pendingRetired.push_back(sprite);
            }
            else if (name == "update_sprite")
            {
                auto *sprite = sprites.at(readId(op.at("id")));
                bool changed = false;
                if (op.contains("center"))
                {
                    const double x = op.at("center")[0].get<double>(), y = op.at("center")[1].get<double>();
                    changed |= sprite->centerX != x || sprite->centerY != y;
                    sprite->centerX = x;
                    sprite->centerY = y;
                }
                if (op.contains("dimensions"))
                {
                    const double x = op.at("dimensions")[0].get<double>(), y = op.at("dimensions")[1].get<double>();
                    changed |= sprite->width != x || sprite->height != y;
                    sprite->width = x;
                    sprite->height = y;
                }
                if (op.contains("scale"))
                {
                    const double x = op.at("scale")[0].get<double>(), y = op.at("scale")[1].get<double>();
                    changed |= sprite->scaleX != x || sprite->scaleY != y;
                    sprite->scaleX = x;
                    sprite->scaleY = y;
                }
                if (op.contains("angle_degrees"))
                {
                    const double value = op.at("angle_degrees").get<double>();
                    changed |= sprite->angle != value;
                    sprite->angle = value;
                }
                geometryChanges += changed;
                if (changed && sprite->active) publishSprite(*sprite);
            }
            else if (name == "set_active")
            {
                auto *sprite = sprites.at(readId(op.at("id")));
                const bool next = op.at("active").get<bool>();
                if (sprite->active != next)
                {
                    if (next)
                    {
                        sprite->active = true;
                        orderAdd(*sprite);
                    }
                    else
                    {
                        orderRemove(*sprite);
                        sprite->active = false;
                    }
                }
            }
            else if (name == "set_view")
            {
                VisibilityTraceView next;
                const auto &value = op.at("view");
                next.id = readId(value.at("id"));
                next.positionX = value.at("position")[0].get<double>();
                next.positionY = value.at("position")[1].get<double>();
                next.width = value.at("size")[0].get<double>();
                next.height = value.at("size")[1].get<double>();
                next.outputWidth = value.at("output")[0].get<int>();
                next.outputHeight = value.at("output")[1].get<int>();
                view = next;
            }
        }
        if (geometryChanges != 0)
            geometryRevision += geometryChanges;
        if (!sameView(oldView, view))
            ++viewRevision;
    }

    const std::vector<VisibilityEndpoint *> &refreshResult()
    {
        if (settings.renderCulling == RenderCullingMode::None) return order;
        const RenderCullingSnapshotView snapshotView{view.positionX, view.positionY, view.width, view.height,
                                                      view.outputWidth, view.outputHeight};
        const auto &visible = culling.prepareSnapshot(&cameraIdentity, snapshotView, snapshotDrawOrder,
                                                      geometryRevision, viewRevision, orderRevision);
        if (!culling.getFrameStats().resultCacheHit)
        {
            result.clear();
            result.reserve(visible.size());
            for (Sprite *token : visible)
            {
                auto *endpoint = endpointBySprite.at(token);
                endpoint->uncullable = culling.isSnapshotSpriteUncullable(token);
                result.push_back(endpoint);
            }
        }
        const auto &stats = culling.getFrameStats();
        if (counters)
        {
            counters->boundsPrepareNanoseconds += stats.boundsPrepareNanoseconds;
            counters->indexUpdateNanoseconds += stats.indexUpdateNanoseconds;
            counters->indexMaintenanceNanoseconds += stats.indexMaintenanceNanoseconds;
            counters->indexQueryNanoseconds += stats.indexQueryNanoseconds;
            counters->resultReduceNanoseconds += stats.resultReduceNanoseconds;
            counters->changedBounds = stats.boundsPrepared;
            counters->proxyInserts += stats.proxyInserts;
            counters->proxyRemoves += stats.proxyRemoves;
            counters->proxyMoves += stats.proxyMoves;
            counters->queries += stats.visibilityQueries;
            counters->rawDiscoveries += stats.rawDiscoveries;
            counters->exactOverlaps += stats.exactChecks;
            counters->rebuildCalls += stats.rebuildCalls;
            counters->rebuildItems += stats.rebuildItems;
            counters->visibilityCacheHits += stats.resultCacheHit;
        }
        return result;
    }

    void releaseRetired()
    {
        for (VisibilityEndpoint *sprite : pendingRetired)
            freeRecords.push_back(sprite);
        pendingRetired.clear();
    }

    void gauges(SpatialReplayCounters &value)
    {
        const auto &stats = culling.getFrameStats();
        value.spriteCount = order.size();
        value.uncullableSprites = stats.uncullableCount;
        value.visibleCandidates = settings.renderCulling == RenderCullingMode::None ? order.size() : stats.visibleCount;
        value.proxyCount = stats.indexProxyCount;
        value.treeHeight = stats.treeHeight;
        value.treeNodes = stats.treeNodes;
        value.visibilityCells = stats.visibilityCells;
    }

    std::vector<std::string> labels(const std::vector<VisibilityEndpoint *> &visible) const
    {
        std::vector<std::string> output;
        output.reserve(visible.size());
        for (const auto *sprite : visible)
            output.push_back(std::to_string(sprite->id.slot) + ":" + std::to_string(sprite->id.generation));
        return output;
    }

    bool actualQuadIntersects(const VisibilityEndpoint &sprite) const
    {
        if (sprite.uncullable)
            return true;
        const float sx = static_cast<float>(view.outputWidth) / static_cast<float>(view.width);
        const float sy = static_cast<float>(view.outputHeight) / static_cast<float>(view.height);
        const float renderW = static_cast<float>(sprite.width) * static_cast<float>(sprite.scaleX);
        const float renderH = static_cast<float>(sprite.height) * static_cast<float>(sprite.scaleY);
        const float x = (static_cast<float>(sprite.centerX) - renderW * 0.5f - static_cast<float>(view.positionX)) * sx;
        const float y = (static_cast<float>(sprite.centerY) - renderH * 0.5f - static_cast<float>(view.positionY)) * sy;
        const float w = renderW * sx, h = renderH * sy;
        const double cx = static_cast<double>(x) + static_cast<double>(w) * 0.5;
        const double cy = static_cast<double>(y) + static_cast<double>(h) * 0.5;
        const double radians = static_cast<double>(static_cast<float>(sprite.angle)) * std::numbers::pi / 180.0;
        const double cosine = std::cos(radians), sine = std::sin(radians);
        std::array<std::array<double, 2>, 4> points{{{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}}};
        for (auto &point : points)
        {
            const double dx = point[0] - cx, dy = point[1] - cy;
            point = {cx + cosine * dx - sine * dy, cy + sine * dx + cosine * dy};
        }
        const double left = -1.0, top = -1.0, right = view.outputWidth + 1.0, bottom = view.outputHeight + 1.0;
        std::array<std::array<double, 2>, 4> axes{{{1, 0}, {0, 1}, {0, 0}, {0, 0}}};
        for (size_t i = 0; i < 2; ++i)
        {
            const auto &a = points[i], &b = points[(i + 1) % 4];
            axes[i + 2] = {-(b[1] - a[1]), b[0] - a[0]};
        }
        const std::array<std::array<double, 2>, 4> rect{{{left, top}, {right, top}, {right, bottom}, {left, bottom}}};
        for (const auto &axis : axes)
        {
            double minA = std::numeric_limits<double>::infinity(), maxA = -minA;
            double minB = std::numeric_limits<double>::infinity(), maxB = -minB;
            for (const auto &point : points)
            {
                const double projection = point[0] * axis[0] + point[1] * axis[1];
                minA = std::min(minA, projection);
                maxA = std::max(maxA, projection);
            }
            for (const auto &point : rect)
            {
                const double projection = point[0] * axis[0] + point[1] * axis[1];
                minB = std::min(minB, projection);
                maxB = std::max(maxB, projection);
            }
            if (maxA < minB || maxB < minA)
                return false;
        }
        return true;
    }

    void validateVisibility(const std::vector<VisibilityEndpoint *> &visible) const
    {
        std::unordered_set<const VisibilityEndpoint *> found(visible.begin(), visible.end());
        if (found.size() != visible.size())
            throw std::runtime_error("Visibility replay emitted duplicate sprite identities");
        size_t previous = 0;
        bool hasPrevious = false;
        for (const auto *sprite : visible)
        {
            if (!sprite->active || (hasPrevious && sprite->orderIndex <= previous))
                throw std::runtime_error("Visibility replay output is inactive or out of retained draw order");
            previous = sprite->orderIndex;
            hasPrevious = true;
        }
        for (const auto *sprite : order)
            if (actualQuadIntersects(*sprite) && !found.contains(sprite))
                throw std::runtime_error("Visibility replay missed an independently projected viewport intersection");
    }

    const SpatialTrace &trace;
    SpatialSettings settings;
    bool validate;
    SpatialReplayCounters *counters;
    std::vector<std::unique_ptr<VisibilityEndpoint>> storage;
    std::vector<VisibilityEndpoint *> freeRecords;
    std::unordered_map<SpatialTraceIdentity, VisibilityEndpoint *, IdentityHash> sprites;
    std::unordered_map<Sprite *, VisibilityEndpoint *> endpointBySprite;
    std::vector<VisibilityEndpoint *> order;
    std::vector<Sprite *> snapshotDrawOrder;
    std::vector<VisibilityEndpoint *> result;
    Camera cameraIdentity;
    RenderCulling culling;
    VisibilityTraceView view;
    std::vector<VisibilityEndpoint *> pendingRetired;
    uint64_t endpointSetupNanoseconds;
    uint64_t geometryRevision;
    uint64_t viewRevision;
    uint64_t orderRevision;
    SpatialReplayResourceObserver resourceObserver;
    void *resourceObserverContext;
};
} // namespace

SpatialReplayRun executeSpatialReplay(const SpatialTrace &trace, const SpatialSettings &settings, bool validate,
                                      SpatialReplayResourceObserver resourceObserver,
                                      void *resourceObserverContext)
{
    if (trace.header.domain == SpatialTraceDomain::Collision)
        return CollisionReplay(trace, settings, validate, resourceObserver, resourceObserverContext).run();
    return VisibilityReplay(trace, settings, validate, resourceObserver, resourceObserverContext).run();
}
} // namespace platformator_detail
