#pragma once

#include "gameobject.h"
#include <memory>

class Collider;

class BoundingRadiusProjection
{
public:
    BoundingRadiusProjection();
    BoundingRadiusProjection(Collider *, float projectedPosition, bool end);
    ~BoundingRadiusProjection();

    Collider *getCollider();
    float getProjectedPosition() const;
    bool isEnd() const;

    void setCollider(Collider *collider);
    void setProjectedPosition(float projectedPosition);
    void setEnd(bool end);

    bool operator==(const BoundingRadiusProjection &other) const;
    bool operator!=(const BoundingRadiusProjection &other) const;
    bool operator<(const BoundingRadiusProjection &other) const;
    bool operator>(const BoundingRadiusProjection &other) const;
    bool operator<=(const BoundingRadiusProjection &other) const;
    bool operator>=(const BoundingRadiusProjection &other) const;

private:
    Collider *collider;
    float projectedPosition;
    bool end;
};

enum class ColliderType
{
    CircleCollider,
    BoxCollider
};

class Collider : public Component
{
public:
    Collider(GameObject *gameObject, ComponentType type);
    ~Collider();

    virtual float getBoundingBoxLengthX() const = 0;
    virtual float getBoundingBoxLengthY() const = 0;
    virtual ColliderType getColliderType() const = 0;
    virtual std::vector<Eigen::Vector2f*> *getNormals(Collider *other) = 0;
    virtual std::unique_ptr<Eigen::Vector2f> projectOntoAxis(const Eigen::Vector2f &axis) = 0;
    virtual std::unique_ptr<Eigen::Vector2f> projectOntoAxis(const Eigen::Vector2f &axis, size_t index) = 0;
    BoundingRadiusProjection &getProjection(const int index);
    BoundingRadiusProjection *getProjections();
    void generateProjections();

    int getLayer() const;
    void setLayer(const int layer);

    bool isTriggered() const;
    void setTrigger(const bool isTrigger);

private:
    int layer;
    bool isTrigger;
    BoundingRadiusProjection projections[4];
};
