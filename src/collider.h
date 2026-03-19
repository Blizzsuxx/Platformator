#pragma once

#include "gameobject.h"
#include <memory>
#include <span>

class Collider;
class LocalSortArray;

class BoundingRadiusProjection
{
public:
    BoundingRadiusProjection();
    BoundingRadiusProjection(Collider *, float projectedPosition, bool isEnd, LocalSortArray *chunk);
    ~BoundingRadiusProjection();

    Collider *getCollider();
    float getProjectedPosition() const;
    bool getIsMaxima() const;
    LocalSortArray *getChunk() const;

    void setCollider(Collider *collider);
    void setProjectedPosition(float projectedPosition);
    void setIsMaxima(bool isMaxima);
    void setChunk(LocalSortArray *chunk);

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

    BoundingRadiusProjectionAxis *getXProjections();
    BoundingRadiusProjectionAxis *getYProjections();

    int getLayer() const;
    void setLayer(const int layer);

    bool getIsTrigger() const;
    void setIsTrigger(const bool isTrigger);

    bool getIsDirty() const;
    void setIsDirty(const bool isDirty);

protected:
    int layer;
    bool isTrigger;
    bool isDirty;

    BoundingRadiusProjectionAxis xProjections;
    BoundingRadiusProjectionAxis yProjections;

    virtual void updateCollider() = 0;
};

template <>
struct ComponentTypeFor<Collider>
{
    static constexpr ComponentType value = ComponentType::COLLIDER;
};
