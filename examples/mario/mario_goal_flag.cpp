#include "mario_goal_flag.h"

#include "mario_game.h"

namespace mario
{
    MarioGoalFlag::MarioGoalFlag(GameObject *gameObject) : MarioEntity(gameObject)
    {
    }

    bool MarioGoalFlag::reach(MarioGame &game)
    {
        if (!isActive())
        {
            return false;
        }

        game.winLevel();
        return true;
    }
} // namespace mario