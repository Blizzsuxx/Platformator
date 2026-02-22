#include "rigidbody.h"

Rigidbody::Rigidbody(GameObject *gameObject) : Component(gameObject, ComponentType::RIGID_BODY), velocity(0.0f, 0.0f), force(0.0f, 0.0f), mass(1.0f), angularVelocity(0.0f), torque(0.0f), momentOfInertia(1.0f), friction(0.5f), bodyType(DYNAMIC), gravity(true)
{
}

Rigidbody::~Rigidbody()
{
}

// Getters
const Eigen::Vector2f &Rigidbody::getVelocity() const
{
    return velocity;
}

const Eigen::Vector2f &Rigidbody::getForce() const
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
void Rigidbody::setVelocity(const Eigen::Vector2f &velocity)
{
    this->velocity = velocity;
}

void Rigidbody::setForce(const Eigen::Vector2f &force)
{
    this->force = force;
}

void Rigidbody::addForce(const Eigen::Vector2f &force)
{
    this->force += force;
}

void Rigidbody::resetForce()
{
    this->force = Eigen::Vector2f(0.0f, 0.0f);
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

void Rigidbody::move(double timeDelta)
{
    if (this->getBodyType() == BodyType::STATIC || this->getGameObject()->getActive() == false)
    {
        return;
    }

    this->setAngularVelocity(this->getAngularVelocity() + this->getTorque() * timeDelta / this->getMomentOfInertia());
    this->getGameObject()->setRotation(this->getGameObject()->getRotation() + this->getAngularVelocity() * timeDelta);
    this->setAngularVelocity(this->getAngularVelocity() * (1.0f - this->getFriction() * timeDelta));

    this->setVelocity(this->getVelocity() + this->getForce() * timeDelta / this->getMass());
    this->getGameObject()->setPosition(this->getGameObject()->getPosition() + this->getVelocity() * timeDelta);
    this->setVelocity(this->getVelocity() * (1.0f - this->getFriction() * timeDelta));
    this->resetForce();
}

void Rigidbody::applyGravity(const Eigen::Vector2f &gravityVector)
{
    if (this->getGravity() == true && this->getBodyType() == BodyType::DYNAMIC)
    {
        this->addForce(gravityVector * this->getMass());
    }
}