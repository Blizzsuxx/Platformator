#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <SDL3/SDL_filesystem.h>

#include "platformator/gameobject.h"
#include "platformator/runner.h"
#include "platformator/runtime.h"
#include "platformator/runtimeoptions.h"
#include "platformator/scriptcomponent.h"

#include "assets/behaviors/citadel_boss.h"
#include "assets/behaviors/citadel_collectible.h"
#include "assets/behaviors/citadel_crate.h"
#include "assets/behaviors/citadel_game.h"
#include "assets/behaviors/citadel_player.h"
#include "assets/behaviors/citadel_portal.h"
#include "assets/behaviors/citadel_projectile.h"
#include "assets/behaviors/citadel_spawner.h"
#include "assets/behaviors/citadel_splitting_slime.h"
#include "assets/behaviors/citadel_turret.h"

namespace
{
    void require(bool condition, const std::string &message)
    {
        if (!condition)
        {
            throw std::runtime_error("Crystal Citadel self-test failed: " + message);
        }
    }

    void stepFrames(platformator::Runtime &runtime, int frames, double dt = 1.0 / 60.0)
    {
        for (int i = 0; i < frames; ++i)
        {
            runtime.simulateFrame(dt);
        }
    }

    template <typename T>
    T *queryBehavior(GameObject *obj)
    {
        if (obj == nullptr)
        {
            return nullptr;
        }
        auto *script = obj->getComponent<ScriptComponent>();
        return script != nullptr ? script->getBehavior<T>() : nullptr;
    }

    template <typename T>
    std::vector<T *> findBehaviors(platformator::Runtime &runtime)
    {
        std::vector<T *> result;
        for (GameObject *obj : runtime.getAllGameObjects())
        {
            if (auto *beh = queryBehavior<T>(obj))
            {
                result.push_back(beh);
            }
        }
        return result;
    }

    int runSelfTest()
    {
        setenv("SDL_VIDEODRIVER", "dummy", 0);
        setenv("SDL_AUDIODRIVER", "dummy", 0);
        setenv("SDL_RENDER_DRIVER", "software", 0);

        RuntimeOptions options;
        options.sceneFilePath = "assets/scenes/hub.scene";
        if (const char *basePath = SDL_GetBasePath())
        {
            std::string root(basePath);
            if (!root.empty() && root.back() != '/' && root.back() != '\\')
            {
                root.push_back('/');
            }
            options.assetRoot = root + "assets";
        }
        options.windowSettings.hidden = true;
        options.windowSettings.headless = false;

        platformator::Runtime runtime(options);
        runtime.loadScene(options.sceneFilePath);
        stepFrames(runtime, 15);

        // 1. Sanctuary Hub verification
        auto *game = citadel::CitadelGameManager::tryGetInstance();
        require(game != nullptr, "CitadelGameManager missing in hub.scene");
        require(game->getLevelTitle() == "Crystal Citadel - Sanctuary Hub", "Unexpected hub title");

        auto *player = game->getPlayer();
        require(player != nullptr, "Player missing in hub.scene");
        require(player->getHp() == player->getMaxHp(), "Player should start at full HP");

        // Fire PlayerBolt template at runtime
        require(player->fireBolt(), "Player failed to instantiate PlayerBolt.pttemplate");
        stepFrames(runtime, 3);
        require(player->getBoltsFired() == 1, "Expected boltsFired == 1");
        auto projectiles = findBehaviors<citadel::CitadelProjectile>(runtime);
        require(!projectiles.empty() && projectiles.front()->isFriendly(), "Friendly PlayerBolt instance not active");

        // Break TrainingCrate -> drops CrystalGem + ShieldCore templates
        GameObject *crateObj = runtime.getGameObject("TrainingCrate");
        require(crateObj != nullptr, "TrainingCrate missing in hub.scene");
        auto *crate = queryBehavior<citadel::CitadelBreakableCrate>(crateObj);
        require(crate != nullptr, "TrainingCrate missing CitadelBreakableCrate");
        crate->takeDamage(1);
        require(crate->isBroken(), "TrainingCrate should report broken");
        stepFrames(runtime, 4);
        require(runtime.getGameObject("TrainingCrate") == nullptr, "Broken TrainingCrate should be destroyed");

        // Collect a ShieldCore -> instantiates parented OrbitShield child on Player
        citadel::CitadelCollectible *shieldCore = nullptr;
        for (auto *item : findBehaviors<citadel::CitadelCollectible>(runtime))
        {
            if (item->getGemValue() == 0)
            {
                shieldCore = item;
                break;
            }
        }
        require(shieldCore != nullptr, "Expected ShieldCore collectible in hub");
        require(shieldCore->collect(player), "Failed to collect ShieldCore");
        stepFrames(runtime, 4);
        require(player->hasActiveShield(), "Collecting ShieldCore should attach OrbitShield child to Player");

        // Transition Hub -> Level 1 (Echoing Caverns)
        GameObject *cavernPortalObj = runtime.getGameObject("PortalToCavern");
        require(cavernPortalObj != nullptr, "PortalToCavern missing in hub.scene");
        auto *cavernPortal = queryBehavior<citadel::CitadelLevelPortal>(cavernPortalObj);
        require(cavernPortal != nullptr && cavernPortal->activatePortal(), "Failed to trigger hub -> cavern portal");
        stepFrames(runtime, 5);

        // 2. Echoing Caverns verification (WaveSpawner + self-referential SplittingSlime)
        game = citadel::CitadelGameManager::tryGetInstance();
        require(game != nullptr && game->getLevelTitle() == "Crystal Citadel - Echoing Caverns", "Did not enter level1_cavern");
        player = game->getPlayer();
        require(player != nullptr, "Player missing in level1_cavern");

        stepFrames(runtime, 20);
        GameObject *spawnerObj = runtime.getGameObject("CavernSpawner");
        require(spawnerObj != nullptr, "CavernSpawner missing in level1_cavern");
        auto *spawner = queryBehavior<citadel::CitadelWaveSpawner>(spawnerObj);
        require(spawner != nullptr, "CavernSpawner missing behavior");
        int beforeSpawn = spawner->getSpawnedCount();
        GameObject *spawnedEnemy = spawner->spawnNextNow();
        require(spawnedEnemy != nullptr && spawner->getSpawnedCount() == beforeSpawn + 1, "WaveSpawner manual wave instantiation failed");

        // Split Slime.pttemplate (self-referential template instantiation)
        GameObject *slimeObj = runtime.getGameObject("CavernSlime_0");
        require(slimeObj != nullptr, "CavernSlime_0 missing in level1_cavern");
        auto *slime = queryBehavior<citadel::CitadelSplittingSlime>(slimeObj);
        require(slime != nullptr && slime->getGeneration() == 1, "CavernSlime_0 should be generation 1");
        int gen2Before = 0;
        for (auto *s : findBehaviors<citadel::CitadelSplittingSlime>(runtime))
        {
            if (s->getGeneration() == 2 && !s->isDefeated())
            {
                ++gen2Before;
            }
        }
        slime->takeDamage(1);
        require(slime->isDefeated(), "Gen-1 slime should split on 1 damage");
        stepFrames(runtime, 4);
        int gen2After = 0;
        for (auto *s : findBehaviors<citadel::CitadelSplittingSlime>(runtime))
        {
            if (s->getGeneration() == 2 && !s->isDefeated())
            {
                ++gen2After;
            }
        }
        require(gen2After >= gen2Before + 2, "Slime split should spawn 2 generation-2 Slime clones");

        // Collect required 3 gems and unlock ExitPortal -> Level 2
        for (int i = 0; i < 3; ++i)
        {
            GameObject *gemObj = runtime.getGameObject("CavernGem_" + std::to_string(i));
            require(gemObj != nullptr, "Missing CavernGem_" + std::to_string(i));
            auto *gem = queryBehavior<citadel::CitadelCollectible>(gemObj);
            require(gem != nullptr && gem->collect(player), "Failed collecting cavern gem");
        }
        stepFrames(runtime, 4);
        require(game->getCollectedGems() >= 3, "Expected collectedGems >= 3 in cavern");

        GameObject *exitPortal1 = runtime.getGameObject("ExitPortal");
        require(exitPortal1 != nullptr, "ExitPortal missing in level1_cavern");
        auto *portal1 = queryBehavior<citadel::CitadelLevelPortal>(exitPortal1);
        require(portal1 != nullptr && portal1->activatePortal(), "Failed to trigger cavern -> foundry portal");
        stepFrames(runtime, 5);

        // 3. Runic Foundry verification (Composite ArcaneTurret internal refs + firing)
        game = citadel::CitadelGameManager::tryGetInstance();
        require(game != nullptr && game->getLevelTitle() == "Crystal Citadel - Runic Foundry", "Did not enter level2_foundry");
        player = game->getPlayer();
        require(player != nullptr, "Player missing in level2_foundry");
        stepFrames(runtime, 10);

        GameObject *turretObj = runtime.getGameObject("FoundryTurret_0");
        require(turretObj != nullptr, "FoundryTurret_0 missing in level2_foundry");
        auto *turret = queryBehavior<citadel::CitadelArcaneTurret>(turretObj);
        require(turret != nullptr && turret->hasResolvedChildren(), "ArcaneTurret failed to resolve child barrel references");
        require(turret->fireShotNow(), "ArcaneTurret failed to fire EnemyBolt.pttemplate");
        stepFrames(runtime, 3);
        bool sawEnemyBolt = false;
        for (auto *proj : findBehaviors<citadel::CitadelProjectile>(runtime))
        {
            if (!proj->isFriendly())
            {
                sawEnemyBolt = true;
                break;
            }
        }
        require(sawEnemyBolt, "Expected active EnemyBolt from ArcaneTurret");

        // Unlock Foundry ExitPortal -> Level 3 (Archon Sanctum)
        for (int i = 0; i < 3; ++i)
        {
            GameObject *gemObj = runtime.getGameObject("FoundryGem_" + std::to_string(i));
            require(gemObj != nullptr, "Missing FoundryGem_" + std::to_string(i));
            auto *gem = queryBehavior<citadel::CitadelCollectible>(gemObj);
            require(gem != nullptr && gem->collect(player), "Failed collecting foundry gem");
        }
        stepFrames(runtime, 4);
        GameObject *exitPortal2 = runtime.getGameObject("ExitPortal");
        require(exitPortal2 != nullptr, "ExitPortal missing in level2_foundry");
        auto *portal2 = queryBehavior<citadel::CitadelLevelPortal>(exitPortal2);
        require(portal2 != nullptr && portal2->activatePortal(), "Failed to trigger foundry -> sanctum portal");
        stepFrames(runtime, 5);

        // 4. Archon Sanctum verification (Composite Boss + defeat portal -> Hub)
        game = citadel::CitadelGameManager::tryGetInstance();
        require(game != nullptr && game->getLevelTitle() == "Crystal Citadel - Archon Sanctum", "Did not enter level3_sanctum");
        stepFrames(runtime, 10);

        GameObject *bossObj = runtime.getGameObject("CrystalArchon");
        require(bossObj != nullptr, "CrystalArchon missing in level3_sanctum");
        auto *boss = queryBehavior<citadel::CitadelArchonBoss>(bossObj);
        require(boss != nullptr && boss->hasResolvedChildren(), "CrystalArchonBoss failed to resolve crown/orb children");
        require(boss->fireVolleyNow(), "CrystalArchonBoss volley failed");
        require(boss->summonMinionNow(), "CrystalArchonBoss minion summon failed");
        stepFrames(runtime, 4);

        boss->takeDamage(boss->getHp() + 5);
        require(boss->isDefeated(), "CrystalArchonBoss should be defeated");
        stepFrames(runtime, 6);
        require(runtime.getGameObject("CrystalArchon") == nullptr, "Defeated boss should be destroyed");

        // Locate dynamically instantiated victory LevelPortal -> hub.scene
        citadel::CitadelLevelPortal *victoryPortal = nullptr;
        for (auto *p : findBehaviors<citadel::CitadelLevelPortal>(runtime))
        {
            if (p->getTargetScenePath() == "assets/scenes/hub.scene")
            {
                victoryPortal = p;
                break;
            }
        }
        require(victoryPortal != nullptr, "Defeating CrystalArchonBoss should instantiate LevelPortal back to hub.scene");
        require(victoryPortal->activatePortal(), "Failed activating dynamically spawned victory portal");
        stepFrames(runtime, 5);

        game = citadel::CitadelGameManager::tryGetInstance();
        require(game != nullptr && game->getLevelTitle() == "Crystal Citadel - Sanctuary Hub", "Did not return to Sanctuary Hub");

        std::cout << "Crystal Citadel self-test PASSED: 4 scenes, parented shield, recursive slime split, composite turret & boss refs verified.\n";
        return 0;
    }
} // namespace

int main(int argc, char *args[])
{
    for (int i = 1; i < argc; ++i)
    {
        if (std::string(args[i]) == "--self-test")
        {
            try
            {
                return runSelfTest();
            }
            catch (const std::exception &ex)
            {
                std::cerr << ex.what() << '\n';
                return 1;
            }
        }
    }
    return platformator::run(argc, args, "assets/scenes/hub.scene");
}
