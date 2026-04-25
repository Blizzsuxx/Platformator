#pragma once

#include "behavior.h"

namespace mario
{
    class MarioEntity : public Behavior
    {
    public:
        bool isActive() const
        {
            return getGameObject()->getActive();
        }
    };
} // namespace mario