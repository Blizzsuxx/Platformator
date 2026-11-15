#pragma once

#include "platformator/animator.h"
#include "platformator/assetreference.h"
#include "platformator/audio.h"
#include "platformator/behavior.h"
#include "platformator/scriptregistration.h"

namespace citadel
{
    class CitadelPlayer;

    class CitadelCollectible : public Behavior
    {
      public:
        CitadelCollectible() = default;

        void start() override;
        void onCollisionEnter(const Collision *collision, Collider *other, double timeDelta) override;

        bool collect(CitadelPlayer *player);
        bool isCollected() const;
        int getGemValue() const;

      private:
        int gemValue = 1;
        int healAmount = 0;
        bool grantsShield = false;
        platformator::AnimationClipRef idleAnim;
        platformator::TemplateAssetRef pickupFxTemplate;
        platformator::AudioAssetRef pickupSound;

        Animator *animator = nullptr;
        Audio *audio = nullptr;
        bool collected = false;

        SERIALIZABLE_SCRIPT(
            CitadelCollectible,
            gemValue,
            healAmount,
            grantsShield,
            idleAnim,
            pickupFxTemplate,
            pickupSound);
    };
} // namespace citadel
