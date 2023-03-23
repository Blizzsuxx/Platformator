#pragma once

#include "gameobject.h"

class Collider : public Component
{
public:
    Collider(GameObject* gameObject);
    virtual ~Collider();
};