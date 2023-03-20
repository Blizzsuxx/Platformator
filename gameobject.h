#pragma once

#include <eigen3/Eigen/Dense>

class GameObject
{
public:
    GameObject();
    ~GameObject();

    void update();
    void render();

    Eigen::Vector2f getPosition() const;
    void setPosition(const Eigen::Vector2f& position);

    Eigen::Vector2f getVelocity() const;
    void setVelocity(const Eigen::Vector2f& velocity);

    Eigen::Vector2f getAcceleration() const;
    void setAcceleration(const Eigen::Vector2f& acceleration);

    float getMass() const;
    void setMass(const float& mass);

    float getRadius() const;
    void setRadius(const float& radius);

    float getFriction() const;
    void setFriction(const float& friction);

    float getElasticity() const;
    void setElasticity(const float& elasticity);

    bool isStatic() const;
    void setStatic(const bool& isStatic);

private:
    Eigen::Vector2f position;
    Eigen::Vector2f velocity;
    Eigen::Vector2f acceleration;

    float mass;
    float radius;
    float friction;
    float elasticity;

    bool isStatic;
};