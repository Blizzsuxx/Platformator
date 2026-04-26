#pragma once

#include "gameobject.h"
#include "gridtypes.h"
#include <memory>
#include <span>

class Collider;
class LocalSortArray;
class GridCell;
class SegmentedIntervalList;
class Edge;
class Collision;

class BoundingRadiusProjection
{
public:
    BoundingRadiusProjection();
    BoundingRadiusProjection(Collider *, float projectedPosition, bool isEnd);
    ~BoundingRadiusProjection();

    Collider *getCollider();
    float getProjectedPosition() const;
    bool getIsMaxima() const;

    void setCollider(Collider *collider);
    void setProjectedPosition(float projectedPosition);
    void setIsMaxima(bool isMaxima);

    bool operator==(const BoundingRadiusProjection &other) const;
    bool operator!=(const BoundingRadiusProjection &other) const;
    bool operator<(const BoundingRadiusProjection &other) const;
    bool operator>(const BoundingRadiusProjection &other) const;
    bool operator<=(const BoundingRadiusProjection &other) const;
    bool operator>=(const BoundingRadiusProjection &other) const;

private:
    Collider *collider;
    float projectedPosition;
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
    bool getIsDirty() const;

    void setBoundingProjection(BoundingRadiusProjection *projection);
    void setChunk(LocalSortArray *chunk);
    void setChunkIndex(size_t chunkIndex);
    void setIsDirty(bool dirty);

    Collider *getCollider();
    float getProjectedPosition() const;
    bool getIsMaxima() const;

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
    bool isDirty;
};

struct BoundingRadiusProjectionAxisProxy
{
    BoundingRadiusProjectionProxy minProxy;
    BoundingRadiusProjectionProxy maxProxy;
    SegmentedIntervalList *ownerList;
    size_t colliderProxyIndex;
};

class BoundingRadiusProjectionAxis
{
public:
    BoundingRadiusProjectionAxis(Collider *collider, float min, float max);
    ~BoundingRadiusProjectionAxis();

    BoundingRadiusProjection *getMin();
    BoundingRadiusProjection *getMax();

    BoundingRadiusProjectionAxisProxy *createProxyForList(SegmentedIntervalList *list);
    BoundingRadiusProjectionAxisProxy *getProxyForList(SegmentedIntervalList *list);
    std::vector<BoundingRadiusProjectionAxisProxy *> &getProxies();
    void removeProxy(SegmentedIntervalList *list);
    void removeProxy(BoundingRadiusProjectionAxisProxy *proxy);

    bool operator==(const BoundingRadiusProjectionAxis &other) const;
    bool operator!=(const BoundingRadiusProjectionAxis &other) const;

private:
    BoundingRadiusProjection min;
    BoundingRadiusProjection max;
    std::vector<BoundingRadiusProjectionAxisProxy *> proxies;
};

enum class ColliderType
{
    CircleCollider,
    BoxCollider
};

enum ColliderFlags : uint8_t
{
    IS_TRIGGER = 1 << 0,
    IS_QUEUED_FOR_ADD = 1 << 1,
    IS_QUEUED_FOR_SYNC = 1 << 2,
    IS_REGISTERED_IN_GRID = 1 << 3
};

class Collider : public Component
{
    friend class GameObject;
    friend class PhysicsManager;

public:
    Collider(GameObject *gameObject, ComponentType type);
    ~Collider();

    virtual ColliderType getColliderType() const = 0;
    virtual Eigen::Vector2f projectOntoAxis(const Eigen::Vector2f &axis) const = 0;
    virtual std::vector<Eigen::Vector2f> getNormals(const Collider *other) const = 0;

    void triggerCollisionEnter(const Collision *collision, Collider *other, double timeDelta) const;
    void triggerCollisionExit(Collider *other, double timeDelta) const;
    void triggerCollisionStay(const Collision *collision, Collider *other, double timeDelta) const;

    BoundingRadiusProjectionAxis *getXProjections();
    BoundingRadiusProjectionAxis *getYProjections();

    uint64_t getCollisionGroup() const;
    void setCollisionGroup(const uint64_t collisionGroup);

    uint64_t getCollisionMask() const;
    void setCollisionMask(const uint64_t collisionMask);

    bool getIsTrigger() const;
    void setIsTrigger(const bool isTrigger);

    bool getIsQueuedForAdd() const;
    void markQueuedForAdd();
    void clearQueuedForAdd();

    void scheduleSync();
    bool getIsQueuedForSync() const;
    void removeSync();

    void applySync();
    uint64_t getStateVersion() const;

    const GridCellRange &getGridCellRange() const;
    GridCellRange calculateGridCellRange();
    void setGridCellRange(const GridCellRange &range);
    bool hasValidGridCellRange() const;

    bool getIsRegisteredInGrid() const;
    void setIsRegisteredInGrid(bool isRegisteredInGrid);

    virtual Edge getEdgeWithNormal(const Eigen::Vector2f &normal) const = 0;

protected:
    uint64_t collisionGroup;
    uint64_t collisionMask;

    uint64_t stateVersion;

    BoundingRadiusProjectionAxis xProjections;
    BoundingRadiusProjectionAxis yProjections;

    GridCellRange gridCellRange;
    ColliderFlags flags;
    size_t pendingAddQueueIndex;
    size_t pendingSyncQueueIndex;

    void repairMinProjectionProxiesForProjection(BoundingRadiusProjection *projection, BoundingRadiusProjectionAxis *axis);
    void repairMaxProjectionProxiesForProjection(BoundingRadiusProjection *projection, BoundingRadiusProjectionAxis *axis);
    void updateGridCellRange();

    void setPendingAddQueueIndex(size_t index);
    size_t getPendingAddQueueIndex() const;

    void setPendingSyncQueueIndex(size_t index);
    size_t getPendingSyncQueueIndex() const;

    virtual void updateCollider() = 0;
};

template <>
struct ComponentTypeFor<Collider>
{
    static constexpr ComponentType value = ComponentType::COLLIDER;
};
