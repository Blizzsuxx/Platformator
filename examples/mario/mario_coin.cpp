#include "mario_coin.h"

#include "mario_game.h"

namespace mario
{
    MarioCoin::MarioCoin(GameObject *gameObject) : MarioEntity(gameObject)
    {
    }

    bool MarioCoin::collect(MarioGame &game)
    {
        if (!isActive())
        {
            return false;
        }

        gameObject->setActive(false);
        game.onCoinCollected();
        return true;
    }
} // namespace mario