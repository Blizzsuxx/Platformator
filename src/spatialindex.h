#pragma once

#include <cstddef>
#include <cstdint>
#include <compare>
#include <functional>
#include <memory>
#include "platformator/spatialsettings.h"

class PhysicsManager;
class PhysicsPairState;

#include "platformator/collider.h"
#include "platformator/objecthandle.h"

namespace platformator_detail
{
enum class SpatialDomain : uint8_t
{
    Physics,
    Visibility,
    CollisionReplay,
    VisibilityReplay
};

enum class SpatialOwnerKind : uint8_t
{
    Collider,
    Sprite,
    Camera
};

enum class SpatialDirty : uint8_t
{
    None = 0,
    Geometry = 1 << 0,
    Filter = 1 << 1,
    Response = 1 << 2
};

constexpr SpatialDirty operator|(SpatialDirty a, SpatialDirty b)
{
    return static_cast<SpatialDirty>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

constexpr SpatialDirty &operator|=(SpatialDirty &a, SpatialDirty b)
{
    a = a | b;
    return a;
}

constexpr bool hasSpatialDirty(SpatialDirty value, SpatialDirty bit)
{
    return (static_cast<uint8_t>(value) & static_cast<uint8_t>(bit)) != 0;
}

struct SpatialIdentity
{
    uint64_t contextId = 0;
    platformator::ObjectHandle endpoint;
    auto operator<=>(const SpatialIdentity &) const = default;
};

struct SpatialBounds
{
    float minX = 0;
    float minY = 0;
    float maxX = 0;
    float maxY = 0;

    bool isFiniteOrdered() const;
    bool overlapsInclusive(const SpatialBounds &other) const;
    SpatialBounds padded(float padding) const;
};

struct SpatialIdentityHash
{
    size_t operator()(const SpatialIdentity &identity) const noexcept;
};

struct SpatialPairPolicy
{
    using Eligibility = bool (*)(void *, ProjectionOwner, ProjectionOwner);
    using Change = void (*)(void *, ProjectionOwner, ProjectionOwner, int);

    void *context = nullptr;
    ProjectionOwnerKind endpointKind = ProjectionOwnerKind::Visibility;
    Eligibility eligible = nullptr;
    Change changed = nullptr;
};

struct SpatialReplayIndexStats
{
    uint64_t updateNanoseconds = 0;
    uint64_t maintenanceNanoseconds = 0;
    uint64_t queryNanoseconds = 0;
    uint64_t reduceNanoseconds = 0;
    size_t proxyInserts = 0;
    size_t proxyRemoves = 0;
    size_t proxyMoves = 0;
    size_t envelopeReuse = 0;
    size_t queries = 0;
    size_t rawDiscoveries = 0;
    size_t exactOverlaps = 0;
    size_t pairAdds = 0;
    size_t pairRemoves = 0;
    size_t rebuildCalls = 0;
    size_t rebuildItems = 0;
    size_t treeHeight = 0;
    size_t treeNodes = 0;
};

// Reusable owner-scoped segmented cells. Each cell reuses the engine's chunked interval lists;
// endpoint identity, pair policy, cell witnesses and retirement are isolated per context.
class SegmentedSpatialContext
{
  public:
    SegmentedSpatialContext(float cellSize, SpatialPairPolicy pairPolicy);
    ~SegmentedSpatialContext();
    SegmentedSpatialContext(const SegmentedSpatialContext &) = delete;
    SegmentedSpatialContext &operator=(const SegmentedSpatialContext &) = delete;

    void insert(ProjectionOwner endpoint, BoundingRadiusProjectionAxis *xAxis,
                BoundingRadiusProjectionAxis *yAxis, const SpatialBounds &bounds);
    void update(ProjectionOwner endpoint, const SpatialBounds &bounds);
    void remove(ProjectionOwner endpoint);
    bool containsPair(ProjectionOwner endpointA, ProjectionOwner endpointB) const;
    size_t getCandidatePairCount() const;
    size_t getOwnedCapacityBytes() const;
    size_t getCellCount() const;
    size_t getCellMembershipCount(ProjectionOwner endpoint) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// A context gives each independent index a distinct identity namespace, even when both indexes
// refer to the same runtime component. It carries no proxy storage or per-endpoint dispatch.
class SpatialContext
{
  public:
    SpatialContext(uint64_t runtimeSerial, SpatialDomain domain);

    SpatialIdentity identify(platformator::ObjectHandle endpoint) const;
    bool owns(const SpatialIdentity &identity) const;
    uint64_t id() const;
    uint64_t runtimeSerial() const;
    SpatialDomain domain() const;

  private:
    uint64_t contextId;
    uint64_t runtimeSerialValue;
    SpatialDomain contextDomain;
};

// Physics-owned dynamic index adapter. It publishes immutable tree batches and reconciles only
// dirty endpoints plus their retained pair adjacency into PhysicsPairState.
class PhysicsSpatialIndex
{
  public:
    PhysicsSpatialIndex(::PhysicsManager *owner, ::PhysicsPairState *pairState, const SpatialSettings &settings);
    ~PhysicsSpatialIndex();
    PhysicsSpatialIndex(const PhysicsSpatialIndex &) = delete;
    PhysicsSpatialIndex &operator=(const PhysicsSpatialIndex &) = delete;

    void queueAdd(Collider *collider);
    void queueRemove(Collider *collider);
    void queueSync(Collider *collider);
    void flush();
    // Internal prepared-bounds ingress used by trace replay. It bypasses shape/cache preparation
    // while retaining this adapter's records, tree maintenance, query reconciliation and pair sink.
    void queuePreparedBounds(Collider *collider, const SpatialBounds &exact, uint64_t group, uint64_t mask,
                             bool active);
    SpatialReplayIndexStats flushPreparedBounds();
    SpatialReplayIndexStats getPreparedBoundsStats() const;
    bool contains(Collider *collider) const;
    size_t getProxyCount() const;
    size_t getRecordCapacity() const;
    size_t getCandidatePairCount() const;
    size_t getOwnedCapacityBytes() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace platformator_detail
