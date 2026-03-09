#include "gameobject.h"

Component::Component(GameObject *gameObject, ComponentType type) : gameObject(gameObject), type(type)
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
