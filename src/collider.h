#pragma once

#include "gameobject.h"
#include <memory>
#include <span>

class Collider;
class LocalSortArray;
class GridCell;
class SegmentedIntervalList;

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

    void setBoundingProjection(BoundingRadiusProjection *projection);
    void setChunk(LocalSortArray *chunk);

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
};

class BoundingRadiusProjectionAxis
{
public:
    BoundingRadiusProjectionAxis(Collider *collider, float min, float max);
    ~BoundingRadiusProjectionAxis();

    BoundingRadiusProjection *getMin();
    BoundingRadiusProjection *getMax();

    bool operator==(const BoundingRadiusProjectionAxis &other) const;
    bool operator!=(const BoundingRadiusProjectionAxis &other) const;

private:
    BoundingRadiusProjection min;
    BoundingRadiusProjection max;
};

struct BoundingRadiusProjectionAxisProxy
{
    BoundingRadiusProjectionProxy minProxy;
    BoundingRadiusProjectionProxy maxProxy;
};

enum class ColliderType
{
    CircleCollider,
    BoxCollider
};

class Collider : public Component
{
    friend class GameObject;

public:
    Collider(GameObject *gameObject, ComponentType type);
    ~Collider();

    virtual ColliderType getColliderType() const = 0;
    virtual Eigen::Vector2f projectOntoAxis(const Eigen::Vector2f &axis) const = 0;
    virtual std::vector<Eigen::Vector2f> getNormals(const Collider *other) const = 0;

    void triggerCollisionEnter(const Collider *other) const;
    void triggerCollisionExit(const Collider *other) const;
    void triggerCollisionStay(const Collider *other) const;

    BoundingRadiusProjectionAxis *getXProjections();
    BoundingRadiusProjectionAxis *getYProjections();

    uint64_t getCollisionGroup() const;
    void setCollisionGroup(const uint64_t collisionGroup);

    uint64_t getCollisionMask() const;
    void setCollisionMask(const uint64_t collisionMask);

    bool getIsTrigger() const;
    void setIsTrigger(const bool isTrigger);

    void updateStateVersion();
    uint64_t getStateVersion() const;

    void addToGridCell(GridCell *cell);
    void removeFromGridCell(GridCell *cell);
    std::vector<GridCell *> &getGridCells();
    void clearGridCells();

    BoundingRadiusProjectionAxisProxy *addProjectionProxyAxis(BoundingRadiusProjection *minProjection, BoundingRadiusProjection *maxProjection, LocalSortArray *chunk);
    void removeProjectionProxy(BoundingRadiusProjectionAxisProxy *proxy);

    BoundingRadiusProjectionAxisProxy *getProjectionProxiesForList(SegmentedIntervalList *list);

protected:
    uint64_t collisionGroup;
    uint64_t collisionMask;

    bool isTrigger;
    uint64_t stateVersion;

    BoundingRadiusProjectionAxis xProjections;
    BoundingRadiusProjectionAxis yProjections;

    std::vector<GridCell *> cells;
    std::vector<BoundingRadiusProjectionAxisProxy *> projectionProxies;

    void setChunkDirtyIfNotNull(LocalSortArray *chunk, const bool isDirty);
    virtual void updateCollider() = 0;
};

template <>
struct ComponentTypeFor<Collider>
{
    static constexpr ComponentType value = ComponentType::COLLIDER;
};
