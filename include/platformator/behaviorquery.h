#pragma once

#include "platformator/behavior.h"
#include "platformator/scriptcomponent.h"

template <typename T>
T *Behavior::getBehavior(GameObject *gameObject) const
{
    ScriptComponent *scriptComponent = gameObject != nullptr ? gameObject->getComponent<ScriptComponent>() : nullptr;
    if (scriptComponent == nullptr)
    {
        return nullptr;
    }

    return scriptComponent->getBehavior<T>();
}