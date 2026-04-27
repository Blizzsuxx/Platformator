#include "mario_goal_flag.h"

#include "animator.h"
#include "mario_game.h"

namespace mario
{
    MarioGoalFlag::MarioGoalFlag()
        : waveAnimset(),
          winSound()
    {
    }

    std::string MarioGoalFlag::getTypeName() const
    {
        return "MarioGoalFlag";
    }

    void MarioGoalFlag::start()
    {
        Animator *animator = getAnimator();
        if (animator != nullptr)
        {
            animator->play(waveAnimset.get());
        }
    }

    bool MarioGoalFlag::reach()
    {
        if (!isActive())
        {
            return false;
        }

        playSound(winSound);
        MarioGame::getInstance().winLevel();
        return true;
    }
} // namespace mario