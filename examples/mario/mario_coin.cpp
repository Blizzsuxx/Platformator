#include "mario_coin.h"

#include "mario_game.h"

namespace mario
{
    MarioCoin::MarioCoin()
    {
    }

    std::string MarioCoin::getTypeName() const
    {
        return "MarioCoin";
    }

    bool MarioCoin::collect()
    {
        if (!isActive())
        {
            return false;
        }

        getGameObject()->setActive(false);
        MarioGame::getInstance().onCoinCollected();
        return true;
    }
} // namespace mario