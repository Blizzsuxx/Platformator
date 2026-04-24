#pragma once

#include "mario_entity.h"

namespace mario
{
    class MarioGame;

    class MarioGoalFlag : public MarioEntity
    {
    public:
        explicit MarioGoalFlag(GameObject *gameObject);

        bool reach(MarioGame &game);
    };
} // namespace mario