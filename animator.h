#pragma once

#include "component.h"

class Animator : public Component
{
public:
    Animator(GameObject* gameObject);
    virtual ~Animator();
};
