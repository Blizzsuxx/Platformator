#pragma once

#include <string>

#include <SDL3/SDL.h>

#include "platformator/assetreference.h"
#include "platformator/audio.h"
#include "platformator/behavior.h"
#include "platformator/objectreference.h"
#include "platformator/scriptregistration.h"

namespace citadel
{
    class CitadelPlayer;

    class CitadelGameManager : public Behavior
    {
      public:
        CitadelGameManager();
        ~CitadelGameManager() override;

        static CitadelGameManager &getInstance();
        static CitadelGameManager *tryGetInstance();

        void start() override;
        void update(double timeDelta) override;

        const std::string &getLevelTitle() const;
        const std::string &getNextScenePath() const;
        const std::string &getHubScenePath() const;
        int getRequiredGems() const;
        int getCollectedGems() const;
        int getEnemiesDefeated() const;
        int getTotalGemsInScene() const;
        bool isLevelCleared() const;
        bool canExitLevel() const;
        CitadelPlayer *getPlayer() const;

        void registerSceneGem(int value);
        void onGemCollected(int value);
        void onEnemyDefeated();
        void onPlayerStatusChanged();
        void triggerLevelTransition(const std::string &targetScene);

      private:
        static CitadelGameManager *instance;

        std::string levelTitle = "Crystal Citadel";
        std::string nextScenePath = "";
        std::string hubScenePath = "assets/scenes/hub.scene";
        int requiredGems = 0;
        int collectedGems = 0;
        int enemiesDefeated = 0;
        ObjectReference<GameObject> player;
        ObjectReference<Audio> bgm;
        platformator::TemplateAssetRef victoryEffectTemplate;

        SDL_Window *window = nullptr;
        int totalGemsInScene = 0;
        bool levelCleared = false;
        bool transitionRequested = false;
        bool key1WasDown = false;
        bool key2WasDown = false;
        bool key3WasDown = false;
        bool key4WasDown = false;
        bool keyNWasDown = false;

        void updateWindowTitle();

        SERIALIZABLE_SCRIPT(
            CitadelGameManager,
            levelTitle,
            nextScenePath,
            hubScenePath,
            requiredGems,
            collectedGems,
            enemiesDefeated,
            player,
            bgm,
            victoryEffectTemplate);
    };
} // namespace citadel
