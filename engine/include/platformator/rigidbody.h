#pragma once

#include <cstddef>
#include <new>

#include <Eigen/Dense>
#include <json.hpp>

#include "platformator/gameobject.h"

enum BodyType
{
    DYNAMIC,
    STATIC,
    KINEMATIC
};

class Rigidbody : public Component
{
    friend class PhysicsManager;

  public:
    PLATFORMATOR_DECLARE_POOLED_ALLOCATORS()

    Rigidbody();
    Rigidbody(GameObject *gameObject);
    Rigidbody(GameObject *gameObject, BodyType bodyType, bool gravity);
    ~Rigidbody();
    Component *clone() const override;

    const Eigen::Vector2f &getVelocity() const
    {
        return velocity;
    }
    const Eigen::Vector2f &getForce() const
    {
        return force;
    }

    float getMass() const
    {
        return mass;
    }

    float getAngularVelocity() const
    {
        return angularVelocity;
    }
    float getTorque() const
    {
        return torque;
    }

    float getMomentOfInertia() const
    {
        return momentOfInertia;
    }
    float getFriction() const
    {
        return friction;
    }
    float getRestitution() const
    {
        return restitution;
    }

    BodyType getBodyType() const
    {
        return bodyType;
    }
    bool getGravity() const
    {
        return gravity;
    }

    Rigidbody *setVelocity(const Eigen::Vector2f &velocity);
    Rigidbody *setForce(const Eigen::Vector2f &force);
    Rigidbody *addForce(const Eigen::Vector2f &force);

    Rigidbody *setMass(float mass);

    Rigidbody *setAngularVelocity(float angularVelocity);
    Rigidbody *setTorque(float torque);

    Rigidbody *setFriction(float friction);
    Rigidbody *setRestitution(float restitution);

    Rigidbody *setBodyType(BodyType bodyType);
    Rigidbody *setGravity(bool gravity);

    float getInverseMass() const noexcept
    {
        return inverseMass;
    }
    float getInverseMomentOfInertia() const noexcept
    {
        return inverseMomentOfInertia;
    }

    Rigidbody *addImpulse(const Eigen::Vector2f &impulse);

    bool getIsSleeping() const
    {
        return sleeping;
    }
    Rigidbody *setIsSleeping(bool sleeping);
    Rigidbody *wakeUp();
    bool hasSupportContact() const
    {
        return supportContactCount > 0;
    }
    size_t getSupportContactCount() const
    {
        return supportContactCount;
    }
    double getSleepTimer() const
    {
        return sleepTimer;
    }
    Rigidbody *setSleepTimer(double sleepTimer);

    bool getIsRegisteredInPhysicsManager() const
    {
        return isRegisteredInPhysicsManager;
    }
    size_t getPhysicsManagerIndex() const
    {
        return physicsManagerIndex == INVALID_INDEX ? SIZE_MAX : static_cast<size_t>(physicsManagerIndex);
    }

    Rigidbody *refreshMomentOfInertiaCache();
    bool qualifiesAsSupportContact(const Eigen::Vector2f &contactDirection,
                                   const Eigen::Vector2f &gravityVectorNormalized) const;

  private:
    static constexpr uint32_t INVALID_INDEX = UINT32_MAX;
    static constexpr uint8_t INTEGRATE_TRANSLATED = 1 << 0;
    static constexpr uint8_t INTEGRATE_ROTATED = 1 << 1;

    // Fills bytes 60..63 after Component so CL 1 starts aligned on velocity.
    int32_t solverIslandId = -1;

    // Cache Line 1 (bytes 64..127): all hot solver, island, and participation state.
    Eigen::Vector2f velocity;
    float angularVelocity;
    float inverseMass;
    float inverseMomentOfInertia;
    float friction;
    float restitution;
    uint32_t solverIslandBodyIndex = INVALID_INDEX;
    uint32_t awakeDynamicIndex = INVALID_INDEX;
    uint32_t solverColorMask = 0;
    uint32_t physicsManagerIndex = INVALID_INDEX;
    BodyType bodyType;
    bool gravity;
    bool sleeping;
    bool isRegisteredInPhysicsManager;
    uint32_t movingKinematicIndex = INVALID_INDEX;
    uint32_t sleepCandidateIndex = INVALID_INDEX;
    uint32_t participationQueueIndex = INVALID_INDEX;

    // Cache Line 2 (bytes 128..167): cold force/mass/sleep/support bookkeeping.
    double sleepTimer;
    Eigen::Vector2f force;
    float torque;
    float mass;
    float momentOfInertia;
    uint32_t supportContactCount;
    int32_t supportDelta = 0;
    uint32_t supportDeltaIndex = INVALID_INDEX;

    bool isAwakeDynamic() const noexcept
    {
        return bodyType == BodyType::DYNAMIC && !sleeping;
    }
    bool isMovingKinematic() const noexcept
    {
        return bodyType == BodyType::KINEMATIC && (!velocity.isZero(0) || angularVelocity != 0.0f);
    }
    bool isMovingBody() const noexcept
    {
        return bodyType != BodyType::STATIC && !sleeping;
    }
    void setIsRegisteredInPhysicsManager(bool registered) noexcept
    {
        isRegisteredInPhysicsManager = registered;
    }
    void setPhysicsManagerIndex(size_t index) noexcept
    {
        physicsManagerIndex = index == SIZE_MAX ? INVALID_INDEX : static_cast<uint32_t>(index);
    }

    void initialize();
    uint8_t integrateLocal(double timeDelta);
    uint8_t integrateDelta(float dx, float dy, float dAngle);
    void integrateForces(double timeDelta, const Eigen::Vector2f &gravityVector);
    void queueParticipationChange();
    float calculateAutomaticMomentOfInertia() const;
};

template <> struct ComponentTypeFor<Rigidbody>
{
    static constexpr ComponentType value = ComponentType::RIGID_BODY;
};

void to_json(nlohmann::json &j, const Rigidbody &rigidbody);
void from_json(const nlohmann::json &j, Rigidbody &rigidbody);
