#include "citadel_game.h"

#include "platformator/behaviorquery.h"

#include "citadel_player.h"

namespace citadel
{
    CitadelGameManager *CitadelGameManager::instance = nullptr;

    CitadelGameManager::CitadelGameManager() = default;

    CitadelGameManager::~CitadelGameManager()
    {
        if (instance == this)
        {
            instance = nullptr;
        }
    }

    CitadelGameManager &CitadelGameManager::getInstance()
    {
        return *instance;
    }

    CitadelGameManager *CitadelGameManager::tryGetInstance()
    {
        return instance;
    }

    void CitadelGameManager::start()
    {
        instance = this;
        window = getRuntime().getWindowHandle();
        levelCleared = false;
        transitionRequested = false;
        updateWindowTitle();
    }

    void CitadelGameManager::update(double)
    {
        auto &runtime = getRuntime();
        const bool key1 = runtime.isKeyDown(SDL_SCANCODE_1);
        const bool key2 = runtime.isKeyDown(SDL_SCANCODE_2);
        const bool key3 = runtime.isKeyDown(SDL_SCANCODE_3);
        const bool key4 = runtime.isKeyDown(SDL_SCANCODE_4);
        const bool keyN = runtime.isKeyDown(SDL_SCANCODE_N);

        if (key1 && !key1WasDown)
        {
            triggerLevelTransition("assets/scenes/hub.scene");
        }
        else if (key2 && !key2WasDown)
        {
            triggerLevelTransition("assets/scenes/level1_cavern.scene");
        }
        else if (key3 && !key3WasDown)
        {
            triggerLevelTransition("assets/scenes/level2_foundry.scene");
        }
        else if (key4 && !key4WasDown)
        {
            triggerLevelTransition("assets/scenes/level3_sanctum.scene");
        }
        else if (keyN && !keyNWasDown && !nextScenePath.empty())
        {
            triggerLevelTransition(nextScenePath);
        }

        key1WasDown = key1;
        key2WasDown = key2;
        key3WasDown = key3;
        key4WasDown = key4;
        keyNWasDown = keyN;
    }

    const std::string &CitadelGameManager::getLevelTitle() const
    {
        return levelTitle;
    }

    const std::string &CitadelGameManager::getNextScenePath() const
    {
        return nextScenePath;
    }

    const std::string &CitadelGameManager::getHubScenePath() const
    {
        return hubScenePath;
    }

    int CitadelGameManager::getRequiredGems() const
    {
        return requiredGems;
    }

    int CitadelGameManager::getCollectedGems() const
    {
        return collectedGems;
    }

    int CitadelGameManager::getEnemiesDefeated() const
    {
        return enemiesDefeated;
    }

    int CitadelGameManager::getTotalGemsInScene() const
    {
        return totalGemsInScene;
    }

    bool CitadelGameManager::isLevelCleared() const
    {
        return levelCleared;
    }

    bool CitadelGameManager::canExitLevel() const
    {
        return collectedGems >= requiredGems;
    }

    CitadelPlayer *CitadelGameManager::getPlayer() const
    {
        return getBehavior<CitadelPlayer>(player.get());
    }

    void CitadelGameManager::registerSceneGem(int value)
    {
        if (value > 0)
        {
            totalGemsInScene += value;
            updateWindowTitle();
        }
    }

    void CitadelGameManager::onGemCollected(int value)
    {
        collectedGems += value;
        if (requiredGems > 0 && collectedGems >= requiredGems && !levelCleared)
        {
            levelCleared = true;
            if (victoryEffectTemplate.hasFilePath() || bool(victoryEffectTemplate))
            {
                if (GameObject *playerObj = player.get())
                {
                    platformator::instantiate(victoryEffectTemplate, playerObj->getPosition());
                }
            }
        }
        updateWindowTitle();
    }

    void CitadelGameManager::onEnemyDefeated()
    {
        ++enemiesDefeated;
        updateWindowTitle();
    }

    void CitadelGameManager::onPlayerStatusChanged()
    {
        updateWindowTitle();
    }

    void CitadelGameManager::triggerLevelTransition(const std::string &targetScene)
    {
        if (transitionRequested || targetScene.empty())
        {
            return;
        }
        transitionRequested = true;
        if (Audio *audioComp = bgm.get())
        {
            audioComp->stop();
        }
        getRuntime().requestLoadScene(targetScene);
    }

    void CitadelGameManager::updateWindowTitle()
    {
        if (window == nullptr)
        {
            return;
        }
        int hp = 0;
        int maxHp = 0;
        bool shield = false;
        if (const CitadelPlayer *p = getPlayer())
        {
            hp = p->getHp();
            maxHp = p->getMaxHp();
            shield = p->hasActiveShield();
        }
        std::string title = "Crystal Citadel - " + levelTitle +
                            " | Gems: " + std::to_string(collectedGems);
        if (requiredGems > 0)
        {
            title += "/" + std::to_string(requiredGems);
        }
        title += " | HP: " + std::to_string(hp) + "/" + std::to_string(maxHp);
        if (shield)
        {
            title += " [SHIELD]";
        }
        title += " | Defeated: " + std::to_string(enemiesDefeated);
        SDL_SetWindowTitle(window, title.c_str());
    }
} // namespace citadel
