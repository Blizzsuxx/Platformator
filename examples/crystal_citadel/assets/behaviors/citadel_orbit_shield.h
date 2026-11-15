#pragma once

#include "platformator/assetreference.h"
#include "platformator/behavior.h"
#include "platformator/scriptregistration.h"

namespace citadel
{
    class CitadelOrbitShield : public Behavior
    {
      public:
        CitadelOrbitShield() = default;

        void start() override;
        void update(double timeDelta) override;

        void absorbHit();
        bool isConsumed() const;

      private:
        float orbitRadius = 34.0f;
        float orbitSpeed = 3.2f;
        float currentAngle = 0.0f;
        platformator::TemplateAssetRef shatterFxTemplate;

        bool consumed = false;

        SERIALIZABLE_SCRIPT(
            CitadelOrbitShield,
            orbitRadius,
            orbitSpeed,
            currentAngle,
            shatterFxTemplate);
    };
} // namespace citadel
