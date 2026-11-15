#pragma once

#include "platformator/assetreference.h"
#include "platformator/audio.h"
#include "platformator/behavior.h"
#include "platformator/scriptregistration.h"

namespace citadel
{
    class CitadelBreakableCrate : public Behavior
    {
      public:
        CitadelBreakableCrate() = default;

        void start() override;
        void takeDamage(int amount);
        bool isBroken() const;

      private:
        int hitPoints = 1;
        int dropCount = 2;
        platformator::TemplateAssetRef lootTemplate;
        platformator::TemplateAssetRef bonusLootTemplate;
        platformator::TemplateAssetRef breakFxTemplate;
        platformator::AudioAssetRef breakSound;

        Audio *audio = nullptr;
        bool broken = false;

        void shatter();

        SERIALIZABLE_SCRIPT(
            CitadelBreakableCrate,
            hitPoints,
            dropCount,
            lootTemplate,
            bonusLootTemplate,
            breakFxTemplate,
            breakSound);
    };
} // namespace citadel
