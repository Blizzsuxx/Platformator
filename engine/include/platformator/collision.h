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

    const Eigen::Vector2f &getNormal() const noexcept
    {
        return normal;
    }
    const ClipPointsWithData &getContactPoints() const noexcept
    {
        return contactPoints;
    }
    ClipPointsWithData &getContactPoints() noexcept
    {
        return contactPoints;
    }

    const Collider *getReferenceObject() const noexcept
    {
        return referenceObject;
    }
    const Collider *getIncidentObject() const noexcept
    {
        return incidentObject;
    }

    CollisionType getResolutionType() const;
    void updateSupportState(const Eigen::Vector2f &gravityVector);

    void setNormal(const Eigen::Vector2f &normal) noexcept
    {
        this->normal = normal;
    }
    void setContactPoints(const ClipPoints &contactPoints);
    void setReferenceObject(const Collider *colliderA) noexcept
    {
        if (referenceObject != colliderA)
            contactPoints.count = 0;
        this->referenceObject = colliderA;
    }
    void setIncidentObject(const Collider *colliderB) noexcept
    {
        this->incidentObject = colliderB;
    }

    bool isVerticalCollision() const;
    bool isHorizontalCollision() const;

    enum SupportState : uint8_t
    {
        NONE_SUPPORT = 0,
        SUPPORTS_REFERENCE = 1 << 0,
        SUPPORTS_INCIDENT = 1 << 1
    };

  private:
    const Collider *referenceObject;
    const Collider *incidentObject;
    Eigen::Vector2f normal;
    ClipPointsWithData contactPoints;
    uint8_t supportState;
};
