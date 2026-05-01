#pragma once

#include "behaviorfactoryregistry.h"
#include "mario_entity.h"

namespace mario
{
    class MarioGoalFlag : public MarioEntity
    {
    public:
        MarioGoalFlag();

        void start() override;

        bool reach();

    private:
        AnimationClip waveAnimset;
        AudioWrapper winSound;

        SERIALIZABLE_SCRIPT(
            MarioGoalFlag,
            waveAnimset,
            winSound);
    };

    REGISTER_SCRIPT(MarioGoalFlag);
} // namespace mario