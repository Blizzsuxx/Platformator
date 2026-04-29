#pragma once

#include "behaviorfactoryregistry.h"
#include "mario_entity.h"

namespace mario
{
    class MarioCoin : public MarioEntity
    {
    public:
        MarioCoin();

        BEHAVIOR_FIELDS(
            MarioCoin,
            BEHAVIOR_FIELD(spinAnimset),
            BEHAVIOR_FIELD(coinSound));

        void start() override;

        bool collect();

    private:
        platformator_behavior_detail::AnimationClipReference spinAnimset;
        platformator_behavior_detail::AudioAssetReference coinSound;
    };

    REGISTER_BEHAVIOR(MarioCoin);
} // namespace mario