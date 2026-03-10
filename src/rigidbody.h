#pragma once

#include <Eigen/Dense>
#include "gameobject.h"

enum BodyType
{
    DYNAMIC,
    STATIC,
    KINEMATIC
};

class Rigidbody : public Component
{
public:
    Rigidbody(GameObject *gameObject);
    Rigidbody(GameObject *gameObject, BodyType bodyType, bool gravity);
    ~Rigidbody();

    void move(double timeDelta);
    void applyGravity(const Eigen::Vector2f &gravityVector);

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
    void setVelocity(const Eigen::Vector2f &velocity);
    void setForce(const Eigen::Vector2f &force);
    void addForce(const Eigen::Vector2f &force);
    void resetForce();

    void setMass(const float mass);

    void setAngularVelocity(const float angularVelocity);
    void setTorque(const float torque);

    void setMomentOfInertia(const float momentOfInertia);
    void setFriction(const float friction);
    void setRestitution(const float restitution);

    void setBodyType(const BodyType bodyType);
    void setGravity(const bool gravity);

    float getInverseMass() const;
    float getInverseMomentOfInertia() const;

    void addImpulse(const Eigen::Vector2f &impulse);

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
};

template <>
struct ComponentTypeFor<Rigidbody>
{
    static constexpr ComponentType value = ComponentType::RIGID_BODY;
};