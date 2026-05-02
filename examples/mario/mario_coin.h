#pragma once

#include "behaviorfactoryregistry.h"
#include "gamemanager.h"
#include "jsonhelpers.h"
#include "behavior.h"
#include "assetreference.h"
#include "objectreference.h"
#include "audio.h"

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