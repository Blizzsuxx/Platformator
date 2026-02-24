#include "gameobject.h"

Component::Component(GameObject *gameObject, ComponentType type) : gameObject(gameObject), type(type)
{
}

Component::~Component()
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
