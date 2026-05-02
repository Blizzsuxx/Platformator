#pragma once

#include "behaviorfactoryregistry.h"
#include "behavior.h"
#include "assetreference.h"
#include "objectreference.h"
#include "jsonhelpers.h"
#include "animator.h"
#include "audio.h"

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