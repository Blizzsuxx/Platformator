#pragma once

#include <cstddef>

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
    Rigidbody();
    Rigidbody(GameObject *gameObject);
    Rigidbody(GameObject *gameObject, BodyType bodyType, bool gravity);
    ~Rigidbody();

    Rigidbody *move(double timeDelta);
    Rigidbody *applyForces(double timeDelta, const Eigen::Vector2f &gravityVector);

    const Eigen::Vector2f &getVelocity() const;
    const Eigen::Vector2f &getForce() const;

    float getMass() const;

    float getAngularVelocity() const;
    float getTorque() const;

    float getMomentOfInertia() const;
    float getFriction() const;
    float getRestitution() const;

    BodyType getBodyType() const;
    bool getGravity() const;

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

    float getInverseMass() const;
    float getInverseMomentOfInertia() const;

    Rigidbody *addImpulse(const Eigen::Vector2f &impulse);

    bool getIsSleeping() const;
    Rigidbody *setIsSleeping(bool sleeping);
    Rigidbody *wakeUp();
    bool hasSupportContact() const;
    size_t getSupportContactCount() const;
    Rigidbody *addSupportContact();
    Rigidbody *removeSupportContact();
    double getSleepTimer() const;
    Rigidbody *setSleepTimer(double sleepTimer);

    bool getIsRegisteredInPhysicsManager() const;
    Rigidbody *setIsRegisteredInPhysicsManager(bool isRegisteredInPhysicsManager);
    size_t getPhysicsManagerIndex() const;
    Rigidbody *setPhysicsManagerIndex(size_t physicsManagerIndex);

    Rigidbody *refreshMomentOfInertiaCache();
    bool qualifiesAsSupportContact(const Eigen::Vector2f &contactDirection,
                                   const Eigen::Vector2f &gravityVectorNormalized) const;

  private:
    Eigen::Vector2f velocity;
    Eigen::Vector2f force;

    float mass;
    float inverseMass;

    float angularVelocity;
    float torque;

    float momentOfInertia;
    float inverseMomentOfInertia;

    float friction;
    float restitution;

    BodyType bodyType;
    bool gravity;
    bool sleeping;
    size_t supportContactCount;
    double sleepTimer;
    bool isRegisteredInPhysicsManager;
    size_t physicsManagerIndex;
    int64_t supportDelta = 0;
    size_t supportDeltaIndex = SIZE_MAX;
    size_t awakeIndex = SIZE_MAX, kinematicIndex = SIZE_MAX, sleepIndex = SIZE_MAX;
    size_t bodyQueueIndex = SIZE_MAX;
    Rigidbody *sleepParent = nullptr;
    uint8_t sleepRank = 0;
    bool islandQuiet = true;
    double islandTimer = 0;

    void initialize();
    uint8_t integrateLocal(double timeDelta);
    void integrateForces(double timeDelta, const Eigen::Vector2f &gravityVector);
    void bodyStateChanged();
    void applySolverImpulse(const Eigen::Vector2f &impulse, const Eigen::Vector2f &arm);
    float calculateAutomaticMomentOfInertia() const;
};

template <> struct ComponentTypeFor<Rigidbody>
{
    static constexpr ComponentType value = ComponentType::RIGID_BODY;
};

void to_json(nlohmann::json &j, const Rigidbody &rigidbody);
void from_json(const nlohmann::json &j, Rigidbody &rigidbody);
