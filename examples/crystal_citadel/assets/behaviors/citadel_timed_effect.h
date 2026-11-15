#pragma once

#include "platformator/animator.h"
#include "platformator/assetreference.h"
#include "platformator/behavior.h"
#include "platformator/scriptregistration.h"

namespace citadel
{
    class CitadelTimedEffect : public Behavior
    {
      public:
        CitadelTimedEffect() = default;

        void start() override;
        void update(double timeDelta) override;

      private:
        float duration = 0.35f;
        float rotationSpeed = 6.0f;
        float riseSpeed = 24.0f;
        platformator::AnimationClipRef clip;

        Animator *animator = nullptr;
        double remainingTime = 0.0;
        bool destroying = false;

        SERIALIZABLE_SCRIPT(
            CitadelTimedEffect,
            duration,
            rotationSpeed,
            riseSpeed,
            clip);
    };
} // namespace citadel
