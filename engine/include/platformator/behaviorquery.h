#pragma once

#include "platformator/behavior.h"
#include "platformator/scriptcomponent.h"

template <typename T>
T *getBehavior(GameObject *gameObject)
{
    ScriptComponent *scriptComponent = gameObject != nullptr ? gameObject->getComponent<ScriptComponent>() : nullptr;
    if (scriptComponent == nullptr)
    {
        return nullptr;
    }

    return scriptComponent->getBehavior<T>();
}

namespace platformator
{
using ::getBehavior;
} // namespace platformator

template <typename T>
T *Behavior::getBehavior(GameObject *gameObject) const
{
    return ::getBehavior<T>(gameObject);
}
