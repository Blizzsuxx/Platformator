#pragma once

#include "doom_constants.h"
#include "platformator/assetreference.h"
#include "platformator/behavior.h"
#include "platformator/scriptregistration.h"
#include "platformator/sprite.h"

namespace doom
{
    class DoomBarrel : public Behavior
    {
      public:
        DoomBarrel();
        ~DoomBarrel() override;

        void start() override;
        void update(double timeDelta) override;
        void onDestroy() override;

        void takeDamage(int damage);
        int getHp() const;
        bool isExploded() const;

      private:
        int hp = 20;
        float blastRadius = 140.0f;
        int blastDamage = 85;
        platformator::TemplateAssetRef explosionTemplate;
        platformator::AudioAssetRef explodeSound;

        Sprite *sprite = nullptr;
        bool registered = false;
        bool exploded = false;
        float flashTimer = 0.0f;

        void ensureRegistered();
        void explode();

        SERIALIZABLE_SCRIPT(
            DoomBarrel,
            hp,
            blastRadius,
            blastDamage,
            explosionTemplate,
            explodeSound);
    };
} // namespace doom
