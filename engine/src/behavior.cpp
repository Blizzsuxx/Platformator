#include "platformator/behavior.h"

#include <stdexcept>

#include "platformator/runtime.h"

Behavior::Behavior() : gameObject(nullptr)
{
}

Behavior::Behavior(const Behavior &other)
    : scriptComponent(nullptr), gameObject(nullptr), detectedUpdateMask(other.detectedUpdateMask)
{
}

Behavior *Behavior::clone() const
{
    throw std::logic_error("Behavior subclass does not implement clone(): " + getTypeName());
}

GameObject *Behavior::getGameObject() const
{
    return gameObject;
}

platformator::Runtime &Behavior::getRuntime() const
{
    return platformator::Runtime::current();
}

void Behavior::setGameObject(GameObject *gameObject)
{
    this->gameObject = gameObject;
}

void Behavior::start()
{
}

void Behavior::update(double)
{
}

void Behavior::fixedUpdate(double)
{
}

void Behavior::lateUpdate(double)
{
}

void Behavior::onDestroy()
{
}

void Behavior::onCollisionEnter(const Collision *, Collider *, double)
{
}

void Behavior::onCollisionExit(Collider *, double)
{
}
