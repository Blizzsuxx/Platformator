#pragma once

#include "platformator/animator.h"
#include "platformator/assetreference.h"
#include "platformator/audio.h"
#include "platformator/behavior.h"
#include "platformator/scriptregistration.h"

namespace mario
{
    class MarioCoin : public Behavior
    {
    public:
        MarioCoin();

        void start() override;

        bool collect();

    private:
        AssetReference<AnimationClip> spinAnimset;
        AssetReference<AudioWrapper> coinSound;
        Audio *audioComponent;
        Animator *animatorComponent;

        SERIALIZABLE_SCRIPT(
            MarioCoin,
            spinAnimset,
            coinSound);
    };
} // namespace mario