#include "doom_game.h"

#include <algorithm>
#include <sstream>

#include "doom_barrel.h"
#include "doom_billboard.h"
#include "doom_enemy.h"
#include "doom_player.h"
#include "platformator/behaviorquery.h"
#include "platformator/runtime.h"

namespace doom
{
    DoomGameManager *DoomGameManager::instance = nullptr;

    DoomGameManager::DoomGameManager() = default;

    DoomGameManager::~DoomGameManager()
    {
        if (instance == this)
        {
            instance = nullptr;
        }
    }

    DoomGameManager &DoomGameManager::getInstance()
    {
        return *instance;
    }

    DoomGameManager *DoomGameManager::tryGetInstance()
    {
        return instance;
    }

    void DoomGameManager::start()
    {
        instance = this;
        window = getRuntime().getWindowHandle();
        transitionRequested = false;
        levelCompleted = false;
        mapViewMode = false;
        redKeyCollected = false;
        kills = 0;
        flashTimer = 0.0f;
        updateWindowTitle();
    }

    void DoomGameManager::onDestroy()
    {
        billboards.clear();
        enemies.clear();
        barrels.clear();
        if (instance == this)
        {
            instance = nullptr;
        }
    }

    void DoomGameManager::update(double timeDelta)
    {
        if (flashTimer > 0.0f)
        {
            flashTimer = std::max(0.0f, flashTimer - static_cast<float>(timeDelta));
        }

        auto &runtime = getRuntime();
        const bool keyN = runtime.isKeyDown(SDL_SCANCODE_N);
        const bool keyF7 = runtime.isKeyDown(SDL_SCANCODE_F7);
        const bool keyF8 = runtime.isKeyDown(SDL_SCANCODE_F8);

        if (keyN && !keyNWasDown && !nextScenePath.empty())
        {
            triggerLevelTransition(nextScenePath);
        }
        else if (keyF7 && !keyF7WasDown)
        {
            triggerLevelTransition("assets/scenes/e1m1_hangar.scene");
        }
        else if (keyF8 && !keyF8WasDown)
        {
            triggerLevelTransition("assets/scenes/e1m2_reactor.scene");
        }

        keyNWasDown = keyN;
        keyF7WasDown = keyF7;
        keyF8WasDown = keyF8;
    }

    DoomPlayer *DoomGameManager::getPlayer() const
    {
        GameObject *playerObj = player.get();
        return playerObj ? platformator::getBehavior<DoomPlayer>(playerObj) : nullptr;
    }

    Camera *DoomGameManager::getCamera() const
    {
        if (auto *cam = camera.get())
        {
            return cam;
        }
        return getRuntime().getMainCamera();
    }

    const std::string &DoomGameManager::getLevelTitle() const
    {
        return levelTitle;
    }

    const std::string &DoomGameManager::getNextScenePath() const
    {
        return nextScenePath;
    }

    const std::string &DoomGameManager::getFirstScenePath() const
    {
        return firstScenePath;
    }

    int DoomGameManager::getLevelIndex() const
    {
        return levelIndex;
    }

    int DoomGameManager::getKills() const
    {
        return kills;
    }

    int DoomGameManager::getTotalEnemies() const
    {
        return totalEnemies;
    }

    bool DoomGameManager::hasRedKey() const
    {
        return redKeyCollected;
    }

    void DoomGameManager::grantRedKey()
    {
        redKeyCollected = true;
        updateWindowTitle();
    }

    bool DoomGameManager::isMapViewMode() const
    {
        return mapViewMode;
    }

    void DoomGameManager::setMapViewMode(bool enabled)
    {
        mapViewMode = enabled;
        updateWindowTitle();
    }

    void DoomGameManager::toggleMapViewMode()
    {
        setMapViewMode(!mapViewMode);
    }

    bool DoomGameManager::isLevelCompleted() const
    {
        return levelCompleted;
    }

    void DoomGameManager::triggerLevelTransition(const std::string &targetScenePath)
    {
        if (transitionRequested || targetScenePath.empty())
        {
            return;
        }
        transitionRequested = true;
        levelCompleted = true;
        getRuntime().requestLoadScene(targetScenePath);
    }

    void DoomGameManager::registerBillboard(DoomBillboard *billboard)
    {
        if (billboard == nullptr)
        {
            return;
        }
        if (std::find(billboards.begin(), billboards.end(), billboard) == billboards.end())
        {
            billboards.push_back(billboard);
        }
    }

    void DoomGameManager::unregisterBillboard(DoomBillboard *billboard)
    {
        std::erase(billboards, billboard);
    }

    const std::vector<DoomBillboard *> &DoomGameManager::getBillboards() const
    {
        return billboards;
    }

    void DoomGameManager::registerEnemy(DoomEnemy *enemy)
    {
        if (enemy == nullptr)
        {
            return;
        }
        if (std::find(enemies.begin(), enemies.end(), enemy) == enemies.end())
        {
            enemies.push_back(enemy);
            ++totalEnemies;
            updateWindowTitle();
        }
    }

    void DoomGameManager::unregisterEnemy(DoomEnemy *enemy)
    {
        std::erase(enemies, enemy);
    }

    const std::vector<DoomEnemy *> &DoomGameManager::getEnemies() const
    {
        return enemies;
    }

    void DoomGameManager::onEnemyDefeated()
    {
        ++kills;
        updateWindowTitle();
    }

    void DoomGameManager::registerBarrel(DoomBarrel *barrel)
    {
        if (barrel == nullptr)
        {
            return;
        }
        if (std::find(barrels.begin(), barrels.end(), barrel) == barrels.end())
        {
            barrels.push_back(barrel);
        }
    }

    void DoomGameManager::unregisterBarrel(DoomBarrel *barrel)
    {
        std::erase(barrels, barrel);
    }

    const std::vector<DoomBarrel *> &DoomGameManager::getBarrels() const
    {
        return barrels;
    }

    void DoomGameManager::triggerScreenFlash(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t maxAlpha,
                                               float duration)
    {
        flashColor = SDL_Color{r, g, b, maxAlpha};
        flashDuration = std::max(0.05f, duration);
        flashTimer = flashDuration;
    }

    SDL_Color DoomGameManager::getActiveScreenFlash() const
    {
        if (flashTimer <= 0.0f || flashDuration <= 0.0f)
        {
            return SDL_Color{0, 0, 0, 0};
        }
        const float ratio = std::clamp(flashTimer / flashDuration, 0.0f, 1.0f);
        const auto alpha = static_cast<std::uint8_t>(static_cast<float>(flashColor.a) * ratio);
        return SDL_Color{flashColor.r, flashColor.g, flashColor.b, alpha};
    }

    void DoomGameManager::onPlayerStatusChanged()
    {
        updateWindowTitle();
    }

    void DoomGameManager::updateWindowTitle()
    {
        if (window == nullptr)
        {
            return;
        }
        std::ostringstream title;
        title << levelTitle;
        if (auto *p = getPlayer())
        {
            title << " | HP " << p->getHp() << " | ARM " << p->getArmor() << " | AMMO " << p->getAmmo();
            if (p->isMouseLookActive())
            {
                title << " | [ESC: Unlock Mouse]";
            }
            else if ((SDL_GetWindowFlags(window) & SDL_WINDOW_HIDDEN) == 0)
            {
                title << " | [Click/L: Lock Mouse]";
            }
        }
        title << " | KILLS " << kills << "/" << totalEnemies;
        if (redKeyCollected)
        {
            title << " | [RED KEY]";
        }
        if (mapViewMode)
        {
            title << " | [2D MAP]";
        }
        SDL_SetWindowTitle(window, title.str().c_str());
    }
} // namespace doom
