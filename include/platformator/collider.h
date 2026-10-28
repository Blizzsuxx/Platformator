#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "platformator/collisiontypes.h"
#include "platformator/gameobject.h"
#include "platformator/gridtypes.h"

class Collider;
class LocalSortArray;
class GridCell;
class SegmentedIntervalList;
class Collision;
class Grid;
class AABB;
namespace platformator_detail
{
class PhysicsSpatialIndex;
}

// Interval endpoints can belong to physics or to the independent render visibility index.
// Keep this compact: endpoint comparisons only inspect positions and min/max ordering.
enum class ProjectionOwnerKind : uint8_t
{
    Collider,
    Visibility
};

struct ProjectionOwner
{
    void *pointer = nullptr;
    ProjectionOwnerKind kind = ProjectionOwnerKind::Collider;

    ProjectionOwner() = default;
    ProjectionOwner(void *pointer, ProjectionOwnerKind kind) : pointer(pointer), kind(kind)
    {
    }
    ProjectionOwner(Collider *collider) : pointer(collider), kind(ProjectionOwnerKind::Collider)
    {
    }

    bool operator==(const ProjectionOwner &other) const
    {
        return pointer == other.pointer && kind == other.kind;
    }
};

struct ProjectionOwnerHash
{
    size_t operator()(const ProjectionOwner &owner) const
    {
        return std::hash<void *>{}(owner.pointer) ^
               (static_cast<size_t>(owner.kind) * size_t(0x9e3779b9));
    }
};

class BoundingRadiusProjection
{
  public:
    BoundingRadiusProjection();
    BoundingRadiusProjection(Collider *collider, float projectedPosition, bool isEnd);
    BoundingRadiusProjection(void *owner, ProjectionOwnerKind ownerKind, float projectedPosition, bool isEnd);
    ~BoundingRadiusProjection();

    Collider *getCollider();
    void *getOwner() const;
    ProjectionOwnerKind getOwnerKind() const;
    ProjectionOwner getEndpoint() const;
    float getProjectedPosition() const;
    bool getIsMaxima() const;

    void setCollider(Collider *collider);
    void setOwner(void *owner, ProjectionOwnerKind ownerKind);
    void setProjectedPosition(float projectedPosition);
    void setIsMaxima(bool isMaxima);

    bool operator==(const BoundingRadiusProjection &other) const;
    bool operator!=(const BoundingRadiusProjection &other) const;
    bool operator<(const BoundingRadiusProjection &other) const;
    bool operator>(const BoundingRadiusProjection &other) const;
    bool operator<=(const BoundingRadiusProjection &other) const;
    bool operator>=(const BoundingRadiusProjection &other) const;

  private:
    void *owner;
    float projectedPosition;
    ProjectionOwnerKind ownerKind;
    bool isMaxima;
};

class BoundingRadiusProjectionProxy
{
  public:
    BoundingRadiusProjectionProxy(BoundingRadiusProjection *projection, LocalSortArray *chunk);
    ~BoundingRadiusProjectionProxy();

    BoundingRadiusProjection *getProjection() const;
    LocalSortArray *getChunk() const;
    size_t getChunkIndex() const;

    void setChunk(LocalSortArray *chunk);
    void setChunkIndex(size_t chunkIndex);
    void updateCachedProjectedPosition();

    Collider *getCollider();
    void *getOwner() const;
    ProjectionOwnerKind getOwnerKind() const;
    ProjectionOwner getEndpoint() const;
    float getProjectedPosition() const;
    bool getIsMaxima() const;
    float getCachedProjectedPosition() const;

    bool operator==(const BoundingRadiusProjectionProxy &other) const;
    bool operator!=(const BoundingRadiusProjectionProxy &other) const;
    bool operator<(const BoundingRadiusProjectionProxy &other) const;
    bool operator>(const BoundingRadiusProjectionProxy &other) const;
    bool operator<=(const BoundingRadiusProjectionProxy &other) const;
    bool operator>=(const BoundingRadiusProjectionProxy &other) const;

  private:
    BoundingRadiusProjection *projection;
    LocalSortArray *chunk;
    size_t chunkIndex;
    float cachedProjectedPosition;
};

class BoundingRadiusProjectionAxis
{
  public:
    BoundingRadiusProjectionAxis(Collider *collider, float min, float max);
    BoundingRadiusProjectionAxis(void *owner, ProjectionOwnerKind ownerKind, float min, float max);
    ~BoundingRadiusProjectionAxis();

    BoundingRadiusProjection *getMin();
    BoundingRadiusProjection *getMax();

    bool operator==(const BoundingRadiusProjectionAxis &other) const;
    bool operator!=(const BoundingRadiusProjectionAxis &other) const;

  private:
    BoundingRadiusProjection min;
    BoundingRadiusProjection max;
};

struct BoundingRadiusProjectionAxisBinding
{
    BoundingRadiusProjectionProxy minProxy;
    BoundingRadiusProjectionProxy maxProxy;

    explicit BoundingRadiusProjectionAxisBinding(BoundingRadiusProjectionAxis *axis);
};

enum class ColliderType
{
    CircleCollider,
    BoxCollider
};

enum ColliderFlags : uint8_t
{
    IS_TRIGGER = 1 << 0,
    IS_REGISTERED_IN_GRID = 1 << 4,
    IS_REGISTERED_IN_PHYSICS_MANAGER = 1 << 5,
    IS_REGISTERED_IN_BROAD_PHASE = 1 << 6
};

class Collider : public Component
{
    friend class GameObject;
    friend class PhysicsManager;
    friend class Grid;
    friend class GridCell;
    friend class platformator_detail::PhysicsSpatialIndex;

  public:
    Collider(GameObject *gameObject, ComponentType type, ColliderType colliderType);
    Collider(ComponentType type, ColliderType colliderType);
    ~Collider();

    ColliderType getColliderType() const;
    virtual Eigen::Vector2f projectOntoAxis(const Eigen::Vector2f &axis) const = 0;
    virtual std::vector<Eigen::Vector2f> getNormals(const Collider *other) const = 0;

    void triggerCollisionEnter(const Collision *collision, Collider *other, double timeDelta) const;
    void triggerCollisionExit(Collider *other, double timeDelta) const;

    BoundingRadiusProjectionAxis *getXProjections();
    BoundingRadiusProjectionAxis *getYProjections();

    uint64_t getCollisionGroup() const;
    void setCollisionGroup(uint64_t collisionGroup);

    uint64_t getCollisionMask() const;
    void setCollisionMask(uint64_t collisionMask);

    bool getIsTrigger() const;
    void setIsTrigger(bool isTrigger);

    const Eigen::Vector2f &getOffset() const;
    void setOffset(const Eigen::Vector2f &offset);
    Eigen::Vector2f getWorldPosition() const;
    const Eigen::Vector2f &getCachedWorldPosition() const
    {
        return cachedWorldPosition;
    }

    bool getIsQueuedForAdd() const;

    bool getIsQueuedForSync() const;
    void scheduleSync();
    void invalidateResponse();

    bool getIsQueuedForRemove() const;

    bool getIsQueuedForAnything() const;

    void prepareSync();
    uint64_t getStateVersion() const;

    const GridCellRange &getGridCellRange() const;
    // True when registered in the selected physics broad phase. Grid diagnostics below retain
    // literal Grid membership and are not set by BVH membership.
    bool getIsRegisteredInBroadPhase() const;
    void calculateGridCellRange();
    const GridCellRange &getGridCellRangeCache() const;
    bool hasValidGridCellRange() const;

    bool getIsRegisteredInGrid() const;
    void setIsRegisteredInGrid(bool isRegisteredInGrid);
    bool getIsRegisteredInPhysicsManager() const;
    void setIsRegisteredInPhysicsManager(bool isRegisteredInPhysicsManager);
    size_t getPhysicsManagerIndex() const;
    void setPhysicsManagerIndex(size_t physicsManagerIndex);

    virtual Edge getEdgeWithNormal(const Eigen::Vector2f &normal) const = 0;

  protected:
    uint64_t collisionGroup;
    uint64_t collisionMask;

    ColliderType colliderType;
    uint64_t stateVersion;
    Eigen::Vector2f offset;
    Eigen::Vector2f cachedWorldPosition = Eigen::Vector2f::Zero();

    BoundingRadiusProjectionAxis xProjections;
    BoundingRadiusProjectionAxis yProjections;
    size_t physicsManagerIndex;

    GridCellRange gridCellRange;
    ColliderFlags flags;
    GridCellRange gridCellRangeCache;

    void updateGridCellRange();

    virtual void updateCollider() = 0;

  private:
    enum Dirty : uint8_t
    {
        Geometry = 1,
        Filter = 2,
        Response = 4
    };
    uint8_t dirty = Geometry;
    bool desiredInBroadPhase = false;
    size_t broadPhaseQueueIndex = SIZE_MAX;
    void setIsRegisteredInBroadPhase(bool isRegistered);
    void prepareGeometrySyncFromCache();
    void prepareSyncFromCache();
};

template <> struct ComponentTypeFor<Collider>
{
    static constexpr ComponentType value = ComponentType::COLLIDER;
};
