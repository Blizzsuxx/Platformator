#pragma once

#include "platformator/assetreference.h"
#include "platformator/audio.h"
#include "platformator/behavior.h"
#include "platformator/scriptregistration.h"

namespace citadel
{
    class CitadelJumpPad : public Behavior
    {
      public:
        CitadelJumpPad() = default;

        void start() override;
        void onCollisionEnter(const Collision *collision, Collider *other, double timeDelta) override;

      private:
        float bounceFactor = 1.55f;
        platformator::TemplateAssetRef burstFxTemplate;
        platformator::AudioAssetRef bounceSound;

        Audio *audio = nullptr;

        SERIALIZABLE_SCRIPT(
            CitadelJumpPad,
            bounceFactor,
            burstFxTemplate,
            bounceSound);
    };
} // namespace citadel
