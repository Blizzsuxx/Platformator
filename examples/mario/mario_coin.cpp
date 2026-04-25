#include "mario_coin.h"

#include "behaviorfactoryregistry.h"
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

    void MarioCoin::deserialize(const ScriptDescriptor &)
    {
    }

    void MarioCoin::serialize(ScriptDescriptor &) const
    {
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

    REGISTER_BEHAVIOR(MarioCoin);
} // namespace mario