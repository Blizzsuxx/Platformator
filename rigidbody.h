#pragma once

#include <eigen3/Eigen/Dense>
#include "component.h"

class Rigidbody : public Component
{
public:
    Rigidbody(GameObject* gameObject);
    ~Rigidbody();

    // Getters
    Eigen::Vector2f getVelocity() const;
    Eigen::Vector2f getForce() const;

    float getMass() const;

    float getAngle() const;
    float getAngularVelocity() const;
    float getTorque() const;

    float getMomentOfInertia() const;
    float getFriction() const;

    // Setters
    void setVelocity(const Eigen::Vector2f& velocity);
    void setForce(const Eigen::Vector2f& force);

    void setMass(const float mass);

    void setAngle(const float angle);
    void setAngularVelocity(const float angularVelocity);
    void setTorque(const float torque);

    void setMomentOfInertia(const float momentOfInertia);
    void setFriction(const float friction);

private:
    Eigen::Vector2f velocity;
    Eigen::Vector2f force;

    float mass;

    float angle;
    float angularVelocity;
    float torque;

    float momentOfInertia;
    float friction;
};