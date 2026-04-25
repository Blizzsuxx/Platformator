#pragma once

#include <string>

#include "gameobject.h"
#include "scriptcomponent.h"

class Animator;

namespace mario
{
    struct Bounds
    {
        Eigen::Vector2f center;
        Eigen::Vector2f halfExtents;
    };

    Bounds getBounds(const GameObject *gameObject);
    void playClipIfChanged(Animator *animator, const std::string &name);

    template <typename T>
    T *getBehavior(GameObject *gameObject)
    {
        ScriptComponent *scriptComponent = gameObject != nullptr ? gameObject->getComponent<ScriptComponent>() : nullptr;
        return scriptComponent != nullptr ? scriptComponent->getBehavior<T>() : nullptr;
    }
} // namespace mario