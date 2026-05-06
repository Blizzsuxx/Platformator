#include "behavior.h"

#include <filesystem>

#include "platformator/runtime.h"

#include "animationclip.h"
#include "collision.h"
#include "gamemanager.h"
#include "scriptcomponent.h"

Behavior::Behavior() : gameObject(nullptr)
{
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

void Behavior::onCollisionStay(const Collision *, Collider *, double)
{
}
