#include "mario_coin.h"

#include "platformator/animator.h"

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
        this->animatorComponent = getGameObject()->getComponent<Animator>();
        this->audioComponent = getGameObject()->getComponent<Audio>();

        if (animatorComponent != nullptr)
        {
            animatorComponent->play(spinAnimset.get());
        }
    }

    bool MarioCoin::collect()
    {
        audioComponent->playAndForget(coinSound.get());
        MarioGame::getInstance().onCoinCollected();
        getGameObject()->destroy();
        return true;
    }
} // namespace mario