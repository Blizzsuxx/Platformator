#pragma once

#include <cstddef>
#include <Eigen/Dense>
#include "gameobject.h"
#include <json.hpp>
#include "jsonhelpers.h"

enum BodyType
{
    DYNAMIC,
    STATIC,
    KINEMATIC
};

class Rigidbody : public Component
{
public:
    Rigidbody();
    Rigidbody(GameObject *gameObject);
    Rigidbody(GameObject *gameObject, BodyType bodyType, bool gravity);
    ~Rigidbody();

    Rigidbody *move(double timeDelta);
    Rigidbody *applyForces(double timeDelta, const Eigen::Vector2f &gravityVector);

    // Getters
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

    // Setters
    Rigidbody *setVelocity(const Eigen::Vector2f &velocity);
    Rigidbody *setForce(const Eigen::Vector2f &force);
    Rigidbody *addForce(const Eigen::Vector2f &force);

    Rigidbody *setMass(const float mass);

    Rigidbody *setAngularVelocity(const float angularVelocity);
    Rigidbody *setTorque(const float torque);

    Rigidbody *setFriction(const float friction);
    Rigidbody *setRestitution(const float restitution);

    Rigidbody *setBodyType(const BodyType bodyType);
    Rigidbody *setGravity(const bool gravity);

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
    bool qualifiesAsSupportContact(const Eigen::Vector2f &contactDirection, const Eigen::Vector2f &gravityVectorNormalized) const;

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

    void initialize();
    float calculateAutomaticMomentOfInertia() const;
};

template <>
struct ComponentTypeFor<Rigidbody>
{
    static constexpr ComponentType value = ComponentType::RIGID_BODY;
};

void to_json(nlohmann::json &j, const Rigidbody &rigidbody);
void from_json(const nlohmann::json &j, Rigidbody &rigidbody);