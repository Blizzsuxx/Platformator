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