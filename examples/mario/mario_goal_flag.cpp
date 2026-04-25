#include "mario_goal_flag.h"

#include "mario_game.h"

namespace mario
{
    MarioGoalFlag::MarioGoalFlag()
    {
    }

    std::string MarioGoalFlag::getTypeName() const
    {
        return "MarioGoalFlag";
    }

    void MarioGoalFlag::deserialize(const ScriptDescriptor &)
    {
    }

    void MarioGoalFlag::serialize(ScriptDescriptor &) const
    {
    }

    bool MarioGoalFlag::reach()
    {
        if (!isActive())
        {
            return false;
        }

        MarioGame::getInstance().winLevel();
        return true;
    }
} // namespace mario