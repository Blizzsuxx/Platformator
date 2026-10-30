#include "mario_goal_flag.h"

#include "mario_game.h"

namespace mario
{
    MarioGoalFlag::MarioGoalFlag()
        : waveAnimset(),
          winSound()
    {
    }

    void MarioGoalFlag::start()
    {
        animator = getGameObject()->getComponent<Animator>();
        audio = getGameObject()->getComponent<Audio>();
        animator->play(waveAnimset.get());
    }

    bool MarioGoalFlag::reach()
    {
        audio->play(winSound.get());
        MarioGame::getInstance().winLevel();
        return true;
    }
} // namespace mario