#pragma once

#include <string>

#include "gameobject.h"

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
} // namespace mario