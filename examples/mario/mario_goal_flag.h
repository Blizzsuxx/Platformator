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
    };

    REGISTER_SCRIPT(
        MarioGoalFlag,
        waveAnimset,
        winSound);
} // namespace mario