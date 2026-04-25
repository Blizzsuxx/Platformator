#include "behavior.h"

#include "scriptcomponent.h"

Behavior::Behavior()
    : gameObject(nullptr), enabled(true)
{
}

GameObject *Behavior::getGameObject() const
{
    return gameObject;
}

bool Behavior::getEnabled() const
{
    return enabled;
}

std::string Behavior::getTypeName() const
{
    return "";
}

void Behavior::deserialize(const ScriptDescriptor &)
{
}

void Behavior::serialize(ScriptDescriptor &) const
{
}

void Behavior::setEnabled(bool enabled)
{
    this->enabled = enabled;
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

void Behavior::onCollisionEnter(Collider *, double)
{
}

void Behavior::onCollisionExit(Collider *, double)
{
}

void Behavior::onCollisionStay(Collider *, double)
{
}

void Behavior::setGameObject(GameObject *gameObject)
{
    this->gameObject = gameObject;
}

GameObject *Behavior::getGameObject()
{
    return gameObject;
}