#include "rigidbody.h"

Rigidbody::Rigidbody(GameObject *gameObject) : Component(gameObject, ComponentType::RIGID_BODY)
{
}

Rigidbody::~Rigidbody()
{
}

// Getters
const Eigen::Vector2f& Rigidbody::getVelocity() const
{
    return velocity;
}

const Eigen::Vector2f& Rigidbody::getForce() const
{
    return force;
}

float Rigidbody::getMass() const
{
    return mass;
}

float Rigidbody::getAngularVelocity() const
{
    return angularVelocity;
}

float Rigidbody::getTorque() const
{
    return torque;
}

float Rigidbody::getMomentOfInertia() const
{
    return momentOfInertia;
}

float Rigidbody::getFriction() const
{
    return friction;
}

BodyType Rigidbody::getBodyType() const
{
    return bodyType;
}

bool Rigidbody::getGravity() const
{
    return gravity;
}

// Setters
void Rigidbody::setVelocity(const Eigen::Vector2f& velocity)
{
    this->velocity = velocity;
}

void Rigidbody::setForce(const Eigen::Vector2f& force)
{
    this->force = force;
}

void Rigidbody::setMass(const float mass)
{
    this->mass = mass;
}

void Rigidbody::setAngularVelocity(const float angularVelocity)
{
    this->angularVelocity = angularVelocity;
}

void Rigidbody::setTorque(const float torque)
{
    this->torque = torque;
}

void Rigidbody::setMomentOfInertia(const float momentOfInertia)
{
    this->momentOfInertia = momentOfInertia;
}

void Rigidbody::setFriction(const float friction)
{
    this->friction = friction;
}

void Rigidbody::setBodyType(const BodyType bodyType)
{
    this->bodyType = bodyType;
}

void Rigidbody::setGravity(const bool gravity)
{
    this->gravity = gravity;
}