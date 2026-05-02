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
        AssetReference<AnimationClip> spinAnimset;
        AssetReference<AudioWrapper> coinSound;

        SERIALIZABLE_SCRIPT(
            MarioCoin,
            spinAnimset,
            coinSound);
    };
} // namespace mario