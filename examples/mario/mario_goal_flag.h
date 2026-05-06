#pragma once

#include "platformator/animator.h"
#include "platformator/assetreference.h"
#include "platformator/audio.h"
#include "platformator/behavior.h"
#include "platformator/scriptregistration.h"

namespace mario
{
    class MarioGoalFlag : public Behavior
    {
    public:
        MarioGoalFlag();

        void start() override;

        bool reach();

    private:
        AssetReference<AnimationClip> waveAnimset;
        AssetReference<AudioWrapper> winSound;
        Animator *animator;
        Audio *audio;

        SERIALIZABLE_SCRIPT(
            MarioGoalFlag,
            waveAnimset,
            winSound);
    };
} // namespace mario