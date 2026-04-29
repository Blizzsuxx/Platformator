#include "mario_coin.h"

#include "animator.h"
#include "mario_game.h"

namespace mario
{
    MarioCoin::MarioCoin()
        : spinAnimset(),
          coinSound()
    {
    }

    void MarioCoin::start()
    {
        Animator *animator = getAnimator();
        if (animator != nullptr)
        {
            animator->play(spinAnimset.get());
        }
    }

    bool MarioCoin::collect()
    {
        if (!isActive())
        {
            return false;
        }

        getGameObject()->setActive(false);
        playSound(coinSound);
        MarioGame::getInstance().onCoinCollected();
        return true;
    }
} // namespace mario