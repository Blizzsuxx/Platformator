#include "gameobject.h"

Component::Component(GameObject *gameObject, ComponentType type) : gameObject(gameObject), type(type)
{
}

Component::Component(ComponentType type) : gameObject(nullptr), type(type)
{
}

GameObject *Component::getGameObject() const
{
    return gameObject;
}

ComponentType Component::getType() const
{
    return type;
}

void Component::setGameObject(GameObject *gameObject)
{
    this->gameObject = gameObject;
}