#pragma once

#include "gameobject.h"

class Animator : public Component
{
public:
    Animator(GameObject *gameObject);
    virtual ~Animator();
};
