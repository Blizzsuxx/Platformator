#pragma once

#include "platformator/assetreference.h"
#include "platformator/behavior.h"
#include "platformator/scriptregistration.h"

namespace citadel
{
    class CitadelWaveSpawner : public Behavior
    {
      public:
        CitadelWaveSpawner() = default;

        void start() override;
        void update(double timeDelta) override;

        GameObject *spawnNextNow();
        int getSpawnedCount() const;
        int getMaxSpawns() const;

      private:
        platformator::TemplateAssetRef primaryEnemyTemplate;
        platformator::TemplateAssetRef secondaryEnemyTemplate;
        platformator::TemplateAssetRef spawnFxTemplate;
        float spawnInterval = 4.0f;
        float activationRange = 640.0f;
        int maxSpawns = 3;
        int spawnedCount = 0;
        bool spawnOnStart = true;

        double timer = 0.0;

        SERIALIZABLE_SCRIPT(
            CitadelWaveSpawner,
            primaryEnemyTemplate,
            secondaryEnemyTemplate,
            spawnFxTemplate,
            spawnInterval,
            activationRange,
            maxSpawns,
            spawnedCount,
            spawnOnStart);
    };
} // namespace citadel
