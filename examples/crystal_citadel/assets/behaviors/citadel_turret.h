#pragma once

#include "platformator/assetreference.h"
#include "platformator/audio.h"
#include "platformator/behavior.h"
#include "platformator/objectreference.h"
#include "platformator/scriptregistration.h"
#include "platformator/sprite.h"

namespace citadel
{
    class CitadelArcaneTurret : public Behavior
    {
      public:
        CitadelArcaneTurret() = default;

        void start() override;
        void update(double timeDelta) override;

        bool fireShotNow();
        bool hasResolvedChildren() const;
        GameObject *getBarrelObject() const;
        Sprite *getBarrelSprite() const;
        int getShotsFired() const;

      private:
        ObjectReference<GameObject> barrelObject;
        ObjectReference<Sprite> barrelSprite;
        platformator::TemplateAssetRef boltTemplate;
        platformator::TemplateAssetRef muzzleFxTemplate;
        platformator::AudioAssetRef fireSound;
        float fireInterval = 2.1f;
        float detectRange = 460.0f;
        float boltSpeed = 230.0f;

        Audio *audio = nullptr;
        double fireTimer = 1.2;
        int shotsFired = 0;

        Eigen::Vector2f computeAimDirection() const;

        SERIALIZABLE_SCRIPT(
            CitadelArcaneTurret,
            barrelObject,
            barrelSprite,
            boltTemplate,
            muzzleFxTemplate,
            fireSound,
            fireInterval,
            detectRange,
            boltSpeed);
    };
} // namespace citadel
