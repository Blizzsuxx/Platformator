#pragma once

#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "doom_constants.h"
#include "platformator/audio.h"
#include "platformator/behavior.h"
#include "platformator/camera.h"
#include "platformator/objectreference.h"
#include "platformator/scriptregistration.h"

namespace doom
{
    class DoomBarrel;
    class DoomBillboard;
    class DoomEnemy;
    class DoomPlayer;

    class DoomGameManager : public Behavior
    {
      public:
        DoomGameManager();
        ~DoomGameManager() override;

        static DoomGameManager &getInstance();
        static DoomGameManager *tryGetInstance();

        void start() override;
        void update(double timeDelta) override;
        void onDestroy() override;

        DoomPlayer *getPlayer() const;
        Camera *getCamera() const;
        const std::string &getLevelTitle() const;
        const std::string &getNextScenePath() const;
        const std::string &getFirstScenePath() const;
        int getLevelIndex() const;

        int getKills() const;
        int getTotalEnemies() const;
        bool hasRedKey() const;
        void grantRedKey();

        bool isMapViewMode() const;
        void setMapViewMode(bool enabled);
        void toggleMapViewMode();

        bool isLevelCompleted() const;
        void triggerLevelTransition(const std::string &targetScenePath);

        void registerBillboard(DoomBillboard *billboard);
        void unregisterBillboard(DoomBillboard *billboard);
        const std::vector<DoomBillboard *> &getBillboards() const;

        void registerEnemy(DoomEnemy *enemy);
        void unregisterEnemy(DoomEnemy *enemy);
        const std::vector<DoomEnemy *> &getEnemies() const;
        void onEnemyDefeated();

        void registerBarrel(DoomBarrel *barrel);
        void unregisterBarrel(DoomBarrel *barrel);
        const std::vector<DoomBarrel *> &getBarrels() const;

        void triggerScreenFlash(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t maxAlpha,
                                float duration);
        SDL_Color getActiveScreenFlash() const;
        void onPlayerStatusChanged();

      private:
        static DoomGameManager *instance;

        std::string levelTitle = "DOOM - Hangar";
        std::string nextScenePath = "assets/scenes/e1m2_reactor.scene";
        std::string firstScenePath = "assets/scenes/e1m1_hangar.scene";
        int levelIndex = 1;
        ObjectReference<GameObject> player;
        ObjectReference<Camera> camera;
        ObjectReference<Audio> bgm;

        SDL_Window *window = nullptr;
        std::vector<DoomBillboard *> billboards;
        std::vector<DoomEnemy *> enemies;
        std::vector<DoomBarrel *> barrels;
        int kills = 0;
        int totalEnemies = 0;
        bool redKeyCollected = false;
        bool mapViewMode = false;
        bool levelCompleted = false;
        bool transitionRequested = false;

        SDL_Color flashColor{0, 0, 0, 0};
        float flashTimer = 0.0f;
        float flashDuration = 0.25f;

        bool keyNWasDown = false;
        bool keyF7WasDown = false;
        bool keyF8WasDown = false;

        void updateWindowTitle();

        SERIALIZABLE_SCRIPT(
            DoomGameManager,
            levelTitle,
            nextScenePath,
            firstScenePath,
            levelIndex,
            player,
            camera,
            bgm);
    };
} // namespace doom
