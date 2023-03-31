#pragma once

#include "gameobject.h"

class Collider : public Component
{
public:
    Collider(GameObject* gameObject, ComponentType type);
    virtual ~Collider();
};