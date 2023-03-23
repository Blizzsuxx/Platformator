#pragma once

#include "component.h"

class Collider : public Component
{
public:
    Collider(GameObject* gameObject);
    virtual ~Collider();
};