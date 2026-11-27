#pragma once

#include <cstdint>

#include "platformator/collider.h"

class PhysicsManager;

class Collision
{
    friend class PhysicsManager;

  public:
    enum CollisionType
    {
        DYNAMIC,
        KINEMATIC,
        TRIGGER,
        NONE_COLLISIONS
    };

    Collision();
    Collision(Collider *colliderA, Collider *colliderB);
    ~Collision() = default;

    const Eigen::Vector2f &getNormal() const;
    const ClipPointsWithData &getContactPoints() const;
    ClipPointsWithData &getContactPoints();

    const Collider *getReferenceObject() const;
    const Collider *getIncidentObject() const;

    CollisionType getResolutionType() const;
    void updateSupportState(const Eigen::Vector2f &gravityVector);

    void setNormal(const Eigen::Vector2f &normal);
    void setContactPoints(const ClipPoints &contactPoints);
    void setReferenceObject(const Collider *colliderA);
    void setIncidentObject(const Collider *colliderB);

    bool isVerticalCollision() const;
    bool isHorizontalCollision() const;

    enum SupportState : uint8_t
    {
        NONE_SUPPORT = 0,
        SUPPORTS_REFERENCE = 1 << 0,
        SUPPORTS_INCIDENT = 1 << 1
    };

  private:
    Eigen::Vector2f normal;
    ClipPointsWithData contactPoints;
    const Collider *referenceObject;
    const Collider *incidentObject;
    uint8_t supportState;
};
