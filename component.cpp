#include "gameobject.h"

Component::Component(GameObject* gameObject, ComponentType type) : gameObject(gameObject), type(type)
{
}

Component::~Component()
{
}

GameObject* Component::getGameObject()
{
    return gameObject;
}

ComponentType Component::getType()
{
    return type;
}
