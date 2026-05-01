#pragma once

#include "behaviorfactoryregistry.h"
#include "mario_entity.h"

namespace mario
{
    class MarioCoin : public MarioEntity
    {
    public:
        MarioCoin();

        void start() override;

        bool collect();

    private:
        AnimationClip spinAnimset;
        AudioWrapper coinSound;

        SERIALIZABLE_SCRIPT(
            MarioCoin,
            spinAnimset,
            coinSound);
    };

    REGISTER_SCRIPT(MarioCoin);
} // namespace mario