#pragma once

#include "gameobject.h"

class Component
{
public:
    Component(GameObject* gameObject);
    virtual ~Component();

    virtual void* getData() = 0;

    GameObject* getGameObject();
private:
    GameObject* gameObject;
};

enum ComponentType
{
    COMPONENT_TYPE_RIGIDBODY = 1,
    COMPONENT_TYPE_RECTCOLLIDER = 2,
    COMPONENT_TYPE_CIRCLECOLLIDER = 4,
    COMPONENT_TYPE_SPRITE = 8,
    
};