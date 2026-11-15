#pragma once

#include <string>

#include "platformator/animator.h"
#include "platformator/assetreference.h"
#include "platformator/audio.h"
#include "platformator/behavior.h"
#include "platformator/scriptregistration.h"

namespace citadel
{
    class CitadelLevelPortal : public Behavior
    {
      public:
        CitadelLevelPortal() = default;

        void start() override;
        void onCollisionEnter(const Collision *collision, Collider *other, double timeDelta) override;

        void setTargetScenePath(const std::string &path);
        const std::string &getTargetScenePath() const;
        bool activatePortal();

      private:
        std::string targetScenePath = "assets/scenes/hub.scene";
        int requiredGems = 0;
        platformator::AnimationClipRef portalAnim;
        platformator::TemplateAssetRef warpFxTemplate;
        platformator::AudioAssetRef warpSound;

        Animator *animator = nullptr;
        Audio *audio = nullptr;
        bool triggered = false;

        SERIALIZABLE_SCRIPT(
            CitadelLevelPortal,
            targetScenePath,
            requiredGems,
            portalAnim,
            warpFxTemplate,
            warpSound);
    };
} // namespace citadel
