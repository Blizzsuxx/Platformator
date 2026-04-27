#pragma once

#include "behaviorfactoryregistry.h"
#include "mario_entity.h"

namespace mario
{
    class MarioCoin : public MarioEntity
    {
    public:
        MarioCoin();

        std::string getTypeName() const override;
        BEHAVIOR_FIELDS(
            MarioCoin,
            BEHAVIOR_FIELD(spinAnimset),
            BEHAVIOR_FIELD(coinSound));

        void start() override;

        bool collect();

    private:
        platformator_behavior_detail::AnimationSetAssetReference spinAnimset;
        platformator_behavior_detail::AudioAssetReference coinSound;
    };

    REGISTER_BEHAVIOR(MarioCoin);
} // namespace mario