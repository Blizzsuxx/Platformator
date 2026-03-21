#include "rigidbody.h"

Rigidbody::Rigidbody(GameObject *gameObject) : Component(gameObject, ComponentType::RIGID_BODY), velocity(0.0f, 0.0f), force(0.0f, 0.0f), mass(1.0f), inverseMass(1.0f / mass), angularVelocity(0.0f), torque(0.0f), momentOfInertia(1.0f), inverseMomentOfInertia(1.0f / momentOfInertia), friction(0.5f), restitution(0.2f), bodyType(DYNAMIC), gravity(true), isRegisteredInPhysicsManager(false), physicsManagerIndex(SIZE_MAX)
{
}

Rigidbody::Rigidbody(GameObject *gameObject, BodyType bodyType, bool gravity) : Component(gameObject, ComponentType::RIGID_BODY), velocity(0.0f, 0.0f), force(0.0f, 0.0f), mass(1.0f), inverseMass(1.0f / mass), angularVelocity(0.0f), torque(0.0f), momentOfInertia(1.0f), inverseMomentOfInertia(1.0f / momentOfInertia), friction(0.5f), restitution(0.2f), bodyType(bodyType), gravity(gravity), isRegisteredInPhysicsManager(false), physicsManagerIndex(SIZE_MAX)
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

float Rigidbody::getRestitution() const
{
    return restitution;
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
    this->inverseMass = (mass != 0.0f) ? 1.0f / mass : 0.0f;
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
    this->inverseMomentOfInertia = (momentOfInertia != 0.0f) ? 1.0f / momentOfInertia : 0.0f;
}

void Rigidbody::setFriction(const float friction)
{
    this->friction = friction;
}

void Rigidbody::setRestitution(const float restitution)
{
    this->restitution = restitution;
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
        this->resetForce();
        return;
    }

    this->setAngularVelocity(this->getAngularVelocity() + this->getTorque() * timeDelta / this->getMomentOfInertia());
    this->getGameObject()->setRotation(this->getGameObject()->getRotation() + this->getAngularVelocity() * timeDelta);

    this->setVelocity(this->getVelocity() + this->getForce() * timeDelta / this->getMass());
    this->getGameObject()->setPosition(this->getGameObject()->getPosition() + this->getVelocity() * timeDelta);
    this->resetForce();
}

void Rigidbody::applyGravity(const Eigen::Vector2f &gravityVector)
{
    if (this->getGravity() == true && this->getBodyType() == BodyType::DYNAMIC)
    {
        this->addForce(gravityVector * this->getMass());
    }
}

float Rigidbody::getInverseMass() const
{
    if (bodyType == BodyType::STATIC)
    {
        return 0.0f;
    }
    return inverseMass;
}

float Rigidbody::getInverseMomentOfInertia() const
{
    if (bodyType == BodyType::STATIC)
    {
        return 0.0f;
    }
    return inverseMomentOfInertia;
}

void Rigidbody::addImpulse(const Eigen::Vector2f &impulse)
{
    if (this->getBodyType() == BodyType::STATIC || this->getGameObject()->getActive() == false)
    {
        return;
    }

    this->setVelocity(this->getVelocity() + impulse * this->getInverseMass());
}

bool Rigidbody::getIsRegisteredInPhysicsManager() const
{
    return isRegisteredInPhysicsManager;
}

void Rigidbody::setIsRegisteredInPhysicsManager(bool isRegisteredInPhysicsManager)
{
    this->isRegisteredInPhysicsManager = isRegisteredInPhysicsManager;
}

size_t Rigidbody::getPhysicsManagerIndex() const
{
    return physicsManagerIndex;
}

void Rigidbody::setPhysicsManagerIndex(size_t physicsManagerIndex)
{
    this->physicsManagerIndex = physicsManagerIndex;
}