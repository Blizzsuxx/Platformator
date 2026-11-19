#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "platformator/behaviorquery.h"
#include "platformator/gameobject.h"
#include "platformator/rigidbody.h"
#include "platformator/runner.h"
#include "platformator/runtime.h"
#include "platformator/runtimeoptions.h"
#include "platformator/sprite.h"

#include "assets/behaviors/doom_barrel.h"
#include "assets/behaviors/doom_billboard.h"
#include "assets/behaviors/doom_door.h"
#include "assets/behaviors/doom_enemy.h"
#include "assets/behaviors/doom_game.h"
#include "assets/behaviors/doom_pickup.h"
#include "assets/behaviors/doom_player.h"
#include "assets/behaviors/doom_projectile.h"
#include "assets/behaviors/doom_raycaster.h"
#include "assets/behaviors/doom_timed_fx.h"
#include "doom_constants.h"

namespace
{
    void require(bool condition, const std::string &message)
    {
        if (!condition)
        {
            throw std::runtime_error("DOOM self-test failed: " + message);
        }
    }

    void stepRenderFrames(platformator::Runtime &runtime, int frames, double dt = 1.0 / 60.0)
    {
        for (int i = 0; i < frames; ++i)
        {
            require(runtime.simulateAndRenderFrame(dt), "Runtime window closed unexpectedly during self-test");
        }
    }

    template <typename T>
    std::vector<T *> findBehaviors(platformator::Runtime &runtime)
    {
        std::vector<T *> result;
        for (GameObject *obj : runtime.getAllGameObjects())
        {
            if (auto *beh = platformator::getBehavior<T>(obj))
            {
                result.push_back(beh);
            }
        }
        return result;
    }

    void aimPlayerAt(doom::DoomPlayer *player, const GameObject *target)
    {
        const Eigen::Vector2f delta = target->getPosition() - player->getGameObject()->getPosition();
        player->setYaw(std::atan2(delta.y(), delta.x()));
    }

    int runSelfTest(const std::string &assetRootOverride)
    {
        setenv("SDL_VIDEODRIVER", "dummy", 0);
        setenv("SDL_AUDIODRIVER", "dummy", 0);
        setenv("SDL_RENDER_DRIVER", "software", 0);

        RuntimeOptions options;
        options.sceneFilePath = "assets/scenes/e1m1_hangar.scene";
        options.assetRoot = assetRootOverride;
        options.windowSettings.hidden = true;
        options.windowSettings.headless = false;
        options.windowSettings.width = 640;
        options.windowSettings.height = 360;

        platformator::Runtime runtime(options);
        stepRenderFrames(runtime, 4);

        // 1. E1M1 Startup & 320-column wall + billboard raycasting
        auto *game = doom::DoomGameManager::tryGetInstance();
        require(game != nullptr && game->getLevelIndex() == 1, "DoomGameManager missing in E1M1");
        auto *player = game->getPlayer();
        require(player != nullptr, "DoomPlayer missing in E1M1");

        // Mouse look defaults ON globally, but stays inactive in hidden self-test windows
        require(doom::DoomPlayer::isMouseLookPreferred(),
                "Mouse look preference must default to enabled");
        require(!player->isMouseLookActive(),
                "Mouse lock must not grab cursor in hidden self-test window");
        player->setMouseLock(false);
        require(!doom::DoomPlayer::isMouseLookPreferred(),
                "setMouseLock(false) should update persisted preference");
        player->setMouseLock(true);
        require(doom::DoomPlayer::isMouseLookPreferred() && !player->isMouseLookActive(),
                "setMouseLock(true) should restore persisted preference without grabbing hidden window");

        GameObject *camObj = runtime.getGameObject("Main Camera");
        require(camObj != nullptr, "Main Camera missing in E1M1");
        auto *raycaster = platformator::getBehavior<doom::DoomRaycaster>(camObj);
        require(raycaster != nullptr, "DoomRaycaster missing on Main Camera");
        require(raycaster->getLastWallHits() == doom::RAY_COLUMNS,
                "Expected full 320-column enclosed wall raycast coverage in E1M1");
        require(raycaster->getLastRenderedBillboardStrips() > 0,
                "Expected visible depth-sliced billboard strips on start sightline in E1M1");

        // 2. Hitscan Pistol raycast against Trooper_SW + HitSparkFx spawning
        GameObject *trooperObj = runtime.getGameObject("Trooper_SW");
        require(trooperObj != nullptr, "Trooper_SW missing in E1M1");
        auto *trooper = platformator::getBehavior<doom::DoomEnemy>(trooperObj);
        require(trooper != nullptr, "Trooper_SW missing DoomEnemy behavior");
        const int trooperHpBefore = trooper->getHp();
        aimPlayerAt(player, trooperObj);
        require(player->fireCurrentWeapon(), "Pistol failed to fire");
        stepRenderFrames(runtime, 2);
        require(trooper->getHp() < trooperHpBefore, "Pistol hitscan raycast did not damage Trooper_SW");

        // 3. Shootable Barrel explosion & cleanup
        GameObject *barrelObj = runtime.getGameObject("Barrel_SW");
        require(barrelObj != nullptr, "Barrel_SW missing in E1M1");
        stepRenderFrames(runtime, 20);
        aimPlayerAt(player, barrelObj);
        require(player->fireCurrentWeapon(), "Pistol failed first barrel shot");
        stepRenderFrames(runtime, 20);
        aimPlayerAt(player, barrelObj);
        require(player->fireCurrentWeapon(), "Pistol failed second barrel shot");
        stepRenderFrames(runtime, 4);
        require(runtime.getGameObject("Barrel_SW") == nullptr, "Barrel_SW should explode and destroy after taking damage");

        // 4. Motorized unlocked Blast Door
        GameObject *doorNorthObj = runtime.getGameObject("DoorNorth");
        require(doorNorthObj != nullptr, "DoorNorth missing in E1M1");
        auto *doorNorth = platformator::getBehavior<doom::DoomDoor>(doorNorthObj);
        require(doorNorth != nullptr && doorNorth->tryActivate(false), "Unlocked DoorNorth failed to activate");
        stepRenderFrames(runtime, 30);
        require(doorNorth->getOpenProgress() > 0.5f, "Unlocked DoorNorth did not slide open");

        // 4b. Physical solid collision blocking (walls, rotated wall, pillar, barrel, closed door)
        auto teleportPlayerAndSettle = [&](const Eigen::Vector2f &targetPos) {
            player->setExternalMoveInput(Eigen::Vector2f::Zero());
            player->getGameObject()->setPosition(targetPos);
            if (auto *rb = player->getGameObject()->getComponent<Rigidbody>())
            {
                rb->setVelocity(Eigen::Vector2f::Zero());
            }
            stepRenderFrames(runtime, 1);
        };

        // 4b.1 Axis-aligned WallWest (X=16, width=32 -> inner face X=32) + NW wall sliding
        teleportPlayerAndSettle(Eigen::Vector2f(120.0f, 700.0f));
        player->setExternalMoveInput(Eigen::Vector2f(-1.0f, 0.0f));
        stepRenderFrames(runtime, 60);
        Eigen::Vector2f colPos = player->getGameObject()->getPosition();
        require(colPos.x() >= 44.0f, "Player walked through solid axis-aligned WallWest");
        player->setExternalMoveInput(Eigen::Vector2f(-1.0f, -1.0f));
        stepRenderFrames(runtime, 20);
        colPos = player->getGameObject()->getPosition();
        require(colPos.x() >= 44.0f && colPos.y() < 685.0f,
                "Player failed to slide vertically along solid WallWest");

        // 4b.2 45-degree rotated AngledBunker_SW ((280, 760), 128x24, rot=-0.785398)
        teleportPlayerAndSettle(Eigen::Vector2f(248.0f, 728.0f));
        player->setExternalMoveInput(Eigen::Vector2f(1.0f, 1.0f));
        stepRenderFrames(runtime, 35);
        colPos = player->getGameObject()->getPosition();
        const Eigen::Vector2f rotatedWallAxis(0.70710678f, 0.70710678f);
        require((colPos - Eigen::Vector2f(280.0f, 760.0f)).dot(rotatedWallAxis) <= -24.0f,
                "Player walked through 45-degree rotated AngledBunker_SW");

        // 4b.3 Cylindrical Pillar_SW ((150, 560), radius=22)
        teleportPlayerAndSettle(Eigen::Vector2f(150.0f, 610.0f));
        player->setExternalMoveInput(Eigen::Vector2f(0.0f, -1.0f));
        stepRenderFrames(runtime, 35);
        colPos = player->getGameObject()->getPosition();
        require((colPos - Eigen::Vector2f(150.0f, 560.0f)).norm() >= 34.0f,
                "Player walked through solid Pillar_SW");

        // 4b.4 Static Barrel_SE_A ((650, 640), radius=13)
        teleportPlayerAndSettle(Eigen::Vector2f(612.0f, 640.0f));
        player->setExternalMoveInput(Eigen::Vector2f(1.0f, 0.0f));
        stepRenderFrames(runtime, 30);
        colPos = player->getGameObject()->getPosition();
        require((colPos - Eigen::Vector2f(650.0f, 640.0f)).norm() >= 25.0f,
                "Player walked through solid Barrel_SE_A");

        // 4b.5 Closed locked kinematic DoorRed ((704, 240), 16x68)
        teleportPlayerAndSettle(Eigen::Vector2f(665.0f, 240.0f));
        player->setExternalMoveInput(Eigen::Vector2f(1.0f, 0.0f));
        stepRenderFrames(runtime, 35);
        colPos = player->getGameObject()->getPosition();
        require(colPos.x() <= 685.0f, "Player walked through closed locked DoorRed");

        // 4b.6 Close-range & edge-clipped billboard/wall UV clamping sweep (Debug std::clamp regression check)
        teleportPlayerAndSettle(Eigen::Vector2f(47.0f, 700.0f));
        player->setYaw(3.14159265f);
        stepRenderFrames(runtime, 1);

        GameObject *barrelSeObj = runtime.getGameObject("Barrel_SE_A");
        require(barrelSeObj != nullptr, "Barrel_SE_A missing in E1M1");
        teleportPlayerAndSettle(Eigen::Vector2f(620.0f, 640.0f));
        const Eigen::Vector2f toBarrelSe = barrelSeObj->getPosition() - player->getGameObject()->getPosition();
        const float baseYaw = std::atan2(toBarrelSe.y(), toBarrelSe.x());
        for (int s = 0; s <= 12; ++s)
        {
            const float offset = -0.85f + 1.70f * (static_cast<float>(s) / 12.0f);
            player->setYaw(baseYaw + offset);
            stepRenderFrames(runtime, 1);
        }

        // Restore spawn state before key/door/pickup/transition tests
        teleportPlayerAndSettle(Eigen::Vector2f(120.0f, 700.0f));
        player->setYaw(-0.45f);
        stepRenderFrames(runtime, 1);
        require(!player->isDead(), "Player died unexpectedly during collision verification");

        // 5. Locked Red Door + Red Keycard collection
        GameObject *doorRedObj = runtime.getGameObject("DoorRed");
        require(doorRedObj != nullptr, "DoorRed missing in E1M1");
        auto *doorRed = platformator::getBehavior<doom::DoomDoor>(doorRedObj);
        require(doorRed != nullptr && !doorRed->tryActivate(game->hasRedKey()),
                "Locked DoorRed must reject activation before RedKeycard is collected");

        GameObject *keyObj = runtime.getGameObject("RedKeycard");
        require(keyObj != nullptr, "RedKeycard missing in E1M1");
        auto *keyPickup = platformator::getBehavior<doom::DoomPickup>(keyObj);
        require(keyPickup != nullptr && keyPickup->collect(player), "Failed collecting RedKeycard");
        stepRenderFrames(runtime, 2);
        require(game->hasRedKey() && doorRed->tryActivate(game->hasRedKey()),
                "DoorRed failed to unlock after collecting RedKeycard");

        // 6. Shotgun pickup & multi-pellet spread firing
        GameObject *shotgunObj = runtime.getGameObject("ShotgunPickup");
        require(shotgunObj != nullptr, "ShotgunPickup missing in E1M1");
        auto *shotgunPickup = platformator::getBehavior<doom::DoomPickup>(shotgunObj);
        require(shotgunPickup != nullptr && shotgunPickup->collect(player), "Failed collecting ShotgunPickup");
        require(player->hasWeapon(doom::WEAPON_SHOTGUN) && player->getCurrentWeapon() == doom::WEAPON_SHOTGUN,
                "Collecting ShotgunPickup should grant and equip the Shotgun");
        stepRenderFrames(runtime, 20);
        const int ammoBeforeShotgun = player->getAmmo();
        require(player->fireCurrentWeapon(), "Shotgun failed to fire");
        require(player->getAmmo() == ammoBeforeShotgun - 2, "Shotgun shot should consume 2 ammo");

        // 7. 2D Map / 3D Viewport camera toggle
        game->setMapViewMode(true);
        stepRenderFrames(runtime, 2);
        require(std::abs(game->getCamera()->getWidth() - 960.0f) < 1.0f,
                "2D map view toggle should zoom camera out over physics arena");
        auto *marker = player->getGameObject()->getComponent<Sprite>();
        require(marker != nullptr, "Player map marker is missing");
        const float yawBeforeMapTurn = player->getYaw();
        for (const float heading : {1.5707963f, -1.5707963f})
        {
            player->setYaw(heading);
            stepRenderFrames(runtime, 1);
            const auto *markerObject = marker->getGameObject();
            const Eigen::Vector2f markerForward(markerObject->getCosRotation(), markerObject->getSinRotation());
            require(markerForward.isApprox(player->getForward(), 1e-5f),
                    "Player map marker should follow player yaw across simulation steps");
        }
        player->setYaw(yawBeforeMapTurn);
        game->setMapViewMode(false);
        stepRenderFrames(runtime, 2);
        require(std::abs(game->getCamera()->getWidth() - doom::VIEW_WIDTH) < 1.0f,
                "Returning from 2D map view should restore 640x360 3D stage camera");

        // 8. Transition to E1M2: Reactor Core via ExitPortal
        GameObject *exitObj = runtime.getGameObject("ExitPortal");
        require(exitObj != nullptr, "ExitPortal missing in E1M1");
        auto *exitPickup = platformator::getBehavior<doom::DoomPickup>(exitObj);
        require(exitPickup != nullptr && exitPickup->collect(player), "Failed activating ExitPortal to E1M2");
        stepRenderFrames(runtime, 4);

        game = doom::DoomGameManager::tryGetInstance();
        require(game != nullptr && game->getLevelIndex() == 2 &&
                    game->getLevelTitle() == "DOOM E1M2 - Reactor Core",
                "Did not transition into E1M2 - Reactor Core");
        player = game->getPlayer();
        camObj = runtime.getGameObject("Main Camera");
        raycaster = camObj ? platformator::getBehavior<doom::DoomRaycaster>(camObj) : nullptr;
        require(player != nullptr && raycaster != nullptr, "Player or DoomRaycaster missing in E1M2");
        require(doom::DoomPlayer::isMouseLookPreferred(),
                "Mouse look preference must persist across level transitions");
        require(!player->isMouseLookActive(),
                "Mouse lock must remain inactive in hidden test windows after transition");
        require(raycaster->getLastWallHits() == doom::RAY_COLUMNS,
                "Expected full 320-column enclosed wall raycast coverage in E1M2");

        // 9. Plasma Rifle pickup + PlayerPlasma.pttemplate projectile instantiation
        GameObject *plasmaObj = runtime.getGameObject("PlasmaPickup");
        require(plasmaObj != nullptr, "PlasmaPickup missing in E1M2");
        auto *plasmaPickup = platformator::getBehavior<doom::DoomPickup>(plasmaObj);
        require(plasmaPickup != nullptr && plasmaPickup->collect(player), "Failed collecting PlasmaPickup");
        require(player->hasWeapon(doom::WEAPON_PLASMA) && player->getCurrentWeapon() == doom::WEAPON_PLASMA,
                "Collecting PlasmaPickup should grant and equip Plasma Rifle");
        require(player->fireCurrentWeapon(), "Plasma Rifle failed to fire");
        stepRenderFrames(runtime, 2);
        bool sawFriendlyPlasma = false;
        for (auto *proj : findBehaviors<doom::DoomProjectile>(runtime))
        {
            if (proj->isFriendly())
            {
                sawFriendlyPlasma = true;
                break;
            }
        }
        require(sawFriendlyPlasma, "Firing Plasma Rifle should instantiate friendly PlayerPlasma projectile");

        // 10. Defeat ReactorBaron -> spawns dropped RedKeycard.pttemplate
        GameObject *baronObj = runtime.getGameObject("ReactorBaron");
        require(baronObj != nullptr, "ReactorBaron boss missing in E1M2");
        auto *baron = platformator::getBehavior<doom::DoomEnemy>(baronObj);
        require(baron != nullptr, "ReactorBaron missing DoomEnemy behavior");
        baron->takeDamage(baron->getHp() + 50);
        require(baron->isDefeated(), "ReactorBaron should be defeated");
        stepRenderFrames(runtime, 4);
        require(runtime.getGameObject("ReactorBaron") == nullptr, "Defeated ReactorBaron should be destroyed");
        require(game->getKills() >= 1, "Defeating ReactorBaron should increment kill counter");

        bool foundDroppedKeycard = false;
        for (auto *p : findBehaviors<doom::DoomPickup>(runtime))
        {
            if (p->getPickupKind() == doom::PICKUP_RED_KEYCARD && !p->isCollected())
            {
                foundDroppedKeycard = true;
                break;
            }
        }
        require(foundDroppedKeycard, "Defeating ReactorBaron must instantiate RedKeycard.pttemplate drop");

        std::cout << "DOOM example self-test PASSED: 320-ray DDA+SAT rendering, rotated & circle walls, "
                  << "depth-sliced billboards, weapons/projectiles, barrels, doors, and 2-level transition verified.\n";
        return 0;
    }
} // namespace

int main(int argc, char *args[])
{
    bool selfTest = false;
    bool customWidth = false;
    bool customHeight = false;
    std::string assetRootOverride;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg(args[i]);
        if (arg == "--self-test")
        {
            selfTest = true;
        }
        else if (arg == "--window-width")
        {
            customWidth = true;
        }
        else if (arg == "--window-height")
        {
            customHeight = true;
        }
        else if (arg == "--asset-root" && i + 1 < argc)
        {
            assetRootOverride = args[++i];
        }
        else if (arg.rfind("--asset-root=", 0) == 0)
        {
            assetRootOverride = arg.substr(std::string("--asset-root=").size());
        }
    }
    if (selfTest)
    {
        try
        {
            return runSelfTest(assetRootOverride);
        }
        catch (const std::exception &ex)
        {
            std::cerr << ex.what() << '\n';
            return 1;
        }
    }

    try
    {
        RuntimeOptions options = parseRuntimeOptions(argc, args, "assets/scenes/e1m1_hangar.scene");
        if (!options.editorSession)
        {
            if (!customWidth)
            {
                options.windowSettings.width = 1280;
            }
            if (!customHeight)
            {
                options.windowSettings.height = 720;
            }
            options.windowSettings.keepAspectRatio = true;
        }
        return platformator::run(options);
    }
    catch (const std::exception &ex)
    {
        std::cerr << "Platformator: " << ex.what() << '\n';
        return 1;
    }
}
