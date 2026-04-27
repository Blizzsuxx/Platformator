#pragma once

#include "behaviorfactoryregistry.h"
#include "mario_entity.h"

namespace mario
{
    class MarioGoalFlag : public MarioEntity
    {
    public:
        MarioGoalFlag();

        std::string getTypeName() const override;
        BEHAVIOR_FIELDS(
            MarioGoalFlag,
            BEHAVIOR_FIELD(waveAnimset),
            BEHAVIOR_FIELD(winSound));

        void start() override;

        bool reach();

    private:
        platformator_behavior_detail::AnimationSetAssetReference waveAnimset;
        platformator_behavior_detail::AudioAssetReference winSound;
    };

    REGISTER_BEHAVIOR(MarioGoalFlag);
} // namespace mario