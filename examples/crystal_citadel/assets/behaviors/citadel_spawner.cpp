#include "citadel_spawner.h"

#include "platformator/behaviorquery.h"

#include "citadel_game.h"
#include "citadel_patrol_enemy.h"
#include "citadel_player.h"
#include "citadel_splitting_slime.h"

namespace citadel
{
    void CitadelWaveSpawner::start()
    {
        timer = spawnOnStart ? 0.15 : spawnInterval;
    }

    void CitadelWaveSpawner::update(double timeDelta)
    {
        if (spawnedCount >= maxSpawns)
        {
            return;
        }

        if (auto *game = CitadelGameManager::tryGetInstance())
        {
            if (auto *player = game->getPlayer())
            {
                const float dist = (player->getGameObject()->getPosition() - getGameObject()->getPosition()).norm();
                if (dist > activationRange)
                {
                    return;
                }
            }
        }

        timer -= timeDelta;
        if (timer <= 0.0)
        {
            timer = spawnInterval;
            spawnNextNow();
        }
    }

    GameObject *CitadelWaveSpawner::spawnNextNow()
    {
        if (spawnedCount >= maxSpawns)
        {
            return nullptr;
        }

        const bool useSecondary = (spawnedCount % 2 == 1) &&
                                  (bool(secondaryEnemyTemplate) || secondaryEnemyTemplate.hasFilePath());
        const platformator::TemplateAssetRef &chosen = useSecondary ? secondaryEnemyTemplate : primaryEnemyTemplate;
        if (!bool(chosen) && !chosen.hasFilePath())
        {
            return nullptr;
        }

        const Eigen::Vector2f pos = getGameObject()->getPosition() + Eigen::Vector2f(0.0f, -24.0f);
        if (bool(spawnFxTemplate) || spawnFxTemplate.hasFilePath())
        {
            platformator::instantiate(spawnFxTemplate, pos);
        }

        GameObject *spawned = platformator::instantiate(chosen, pos);
        if (spawned != nullptr)
        {
            const float dir = (spawnedCount % 2 == 0) ? -1.0f : 1.0f;
            if (auto *patrol = getBehavior<CitadelPatrolEnemy>(spawned))
            {
                patrol->configurePatrol(pos.x() - 120.0f, pos.x() + 120.0f, dir);
            }
            if (auto *slime = getBehavior<CitadelSplittingSlime>(spawned))
            {
                slime->configureSpawn(1, dir, SLIME_WALK_SPEED, pos.x() - 130.0f, pos.x() + 130.0f, 1);
            }
            ++spawnedCount;
        }
        return spawned;
    }

    int CitadelWaveSpawner::getSpawnedCount() const
    {
        return spawnedCount;
    }

    int CitadelWaveSpawner::getMaxSpawns() const
    {
        return maxSpawns;
    }
} // namespace citadel
