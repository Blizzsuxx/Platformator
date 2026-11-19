#include "doom_player.h"

#include <algorithm>
#include <cmath>

#include "doom_barrel.h"
#include "doom_door.h"
#include "doom_enemy.h"
#include "doom_game.h"
#include "doom_projectile.h"
#include "platformator/behaviorquery.h"
#include "platformator/runtime.h"

namespace doom
{
    bool DoomPlayer::mouseLookPreferred = true;

    DoomPlayer::DoomPlayer() = default;

    void DoomPlayer::start()
    {
        body = getGameObject()->getComponent<Rigidbody>();
        audio = getGameObject()->getComponent<Audio>();
        spawnPosition = getGameObject()->getPosition();
        setYaw(startYaw);
        if (maxHp <= 0)
        {
            maxHp = 100;
        }
        if (hp <= 0)
        {
            hp = maxHp;
        }
        if (body != nullptr)
        {
            body->setGravity(false);
            body->setFriction(0.0f);
            body->setAngularVelocity(0.0f);
            body->setVelocity(Eigen::Vector2f::Zero());
        }

        auto &runtime = getRuntime();
        escapeWasDown = runtime.isKeyDown(SDL_SCANCODE_ESCAPE);
        mouseToggleWasDown = runtime.isKeyDown(SDL_SCANCODE_L);
        const SDL_MouseButtonFlags mouseButtons = runtime.getMouseState(nullptr, nullptr);
        leftMouseWasDown = (mouseButtons & SDL_BUTTON_LMASK) != 0;
        suppressMouseFireUntilRelease = false;
        fireWasDown = runtime.isKeyDown(SDL_SCANCODE_SPACE) || runtime.isKeyDown(SDL_SCANCODE_F) ||
                      runtime.isKeyDown(SDL_SCANCODE_LCTRL) || leftMouseWasDown;
        useWasDown = runtime.isKeyDown(SDL_SCANCODE_E) || runtime.isKeyDown(SDL_SCANCODE_RETURN);
        mapWasDown = runtime.isKeyDown(SDL_SCANCODE_M) || runtime.isKeyDown(SDL_SCANCODE_TAB);
        respawnWasDown = runtime.isKeyDown(SDL_SCANCODE_R);
        externalMoveInput.setZero();

        applyMouseLock(mouseLookPreferred);
        if (auto *game = DoomGameManager::tryGetInstance())
        {
            game->onPlayerStatusChanged();
        }
    }

    void DoomPlayer::fixedUpdate(double timeDelta)
    {
        const float dt = static_cast<float>(timeDelta);
        if (fireCooldownTimer > 0.0f)
        {
            fireCooldownTimer = std::max(0.0f, fireCooldownTimer - dt);
        }
        if (weaponFireVisualTimer > 0.0f)
        {
            weaponFireVisualTimer = std::max(0.0f, weaponFireVisualTimer - dt);
        }
        if (muzzleLight > 0.0f)
        {
            muzzleLight = std::max(0.0f, muzzleLight - dt * 5.0f);
        }
        if (recoil > 0.0f)
        {
            recoil = std::max(0.0f, recoil - dt * 4.5f);
        }
        if (hurtCooldown > 0.0f)
        {
            hurtCooldown = std::max(0.0f, hurtCooldown - dt);
        }

        if (body != nullptr)
        {
            body->setAngularVelocity(0.0f);
        }
        auto &runtime = getRuntime();
        auto *game = DoomGameManager::tryGetInstance();

        // Mouse lock management (runs even while dead so Esc always frees the cursor)
        SDL_Window *win = runtime.getWindowHandle();
        const SDL_WindowFlags winFlags = win ? SDL_GetWindowFlags(win) : 0;
        const bool interactiveWindow = (win != nullptr) && ((winFlags & SDL_WINDOW_HIDDEN) == 0);

        if (mouseLookPreferred && mouseLockSyncPending && interactiveWindow &&
            (winFlags & SDL_WINDOW_INPUT_FOCUS) != 0)
        {
            if (SDL_SetWindowRelativeMouseMode(win, true))
            {
                mouseLockSyncPending = false;
                float dx = 0.0f, dy = 0.0f;
                SDL_GetRelativeMouseState(&dx, &dy);
            }
        }

        const bool escapeDown = runtime.isKeyDown(SDL_SCANCODE_ESCAPE);
        if (escapeDown && !escapeWasDown)
        {
            applyMouseLock(false);
        }
        escapeWasDown = escapeDown;

        const bool mouseToggleDown = runtime.isKeyDown(SDL_SCANCODE_L);
        if (mouseToggleDown && !mouseToggleWasDown)
        {
            applyMouseLock(!mouseLookPreferred);
        }
        mouseToggleWasDown = mouseToggleDown;

        const SDL_MouseButtonFlags mouseButtons = runtime.getMouseState(nullptr, nullptr);
        const bool leftMouseDown = (mouseButtons & SDL_BUTTON_LMASK) != 0;
        if (!leftMouseDown)
        {
            suppressMouseFireUntilRelease = false;
        }
        if (!mouseLookActive && leftMouseDown && !leftMouseWasDown && interactiveWindow &&
            (winFlags & SDL_WINDOW_MOUSE_FOCUS) != 0)
        {
            applyMouseLock(true);
            suppressMouseFireUntilRelease = true;
        }
        leftMouseWasDown = leftMouseDown;

        const bool respawnDown = runtime.isKeyDown(SDL_SCANCODE_R);
        if (respawnDown && !respawnWasDown)
        {
            if (isDead())
            {
                respawn();
            }
        }
        respawnWasDown = respawnDown;

        if (isDead())
        {
            if (body != nullptr)
            {
                body->setVelocity(Eigen::Vector2f::Zero());
            }
            return;
        }

        // Toggle 2D/3D view with M or Tab
        const bool mapDown = runtime.isKeyDown(SDL_SCANCODE_M) || runtime.isKeyDown(SDL_SCANCODE_TAB);
        if (mapDown && !mapWasDown && game != nullptr)
        {
            game->toggleMapViewMode();
        }
        mapWasDown = mapDown;

        // Weapon selection keys 1 / 2 / 3
        if (runtime.isKeyDown(SDL_SCANCODE_1))
        {
            selectWeapon(WEAPON_PISTOL);
        }
        else if (runtime.isKeyDown(SDL_SCANCODE_2))
        {
            selectWeapon(WEAPON_SHOTGUN);
        }
        else if (runtime.isKeyDown(SDL_SCANCODE_3))
        {
            selectWeapon(WEAPON_PLASMA);
        }

        // Turning
        float turnInput = 0.0f;
        if (runtime.isKeyDown(SDL_SCANCODE_LEFT) || runtime.isKeyDown(SDL_SCANCODE_Q))
        {
            turnInput -= 1.0f;
        }
        if (runtime.isKeyDown(SDL_SCANCODE_RIGHT))
        {
            turnInput += 1.0f;
        }
        float turnDelta = turnInput * turnSpeed * dt;

        if (mouseLookActive && win != nullptr)
        {
            float relX = 0.0f, relY = 0.0f;
            SDL_GetRelativeMouseState(&relX, &relY);
            turnDelta += relX * 0.0028f;
        }
        if (turnDelta != 0.0f)
        {
            setYaw(yaw + turnDelta);
        }

        // Translation relative to current yaw
        const Eigen::Vector2f fwd = getForward();
        const Eigen::Vector2f right = getRight();
        Eigen::Vector2f moveDir = externalMoveInput;
        if (runtime.isKeyDown(SDL_SCANCODE_W) || runtime.isKeyDown(SDL_SCANCODE_UP))
        {
            moveDir += fwd;
        }
        if (runtime.isKeyDown(SDL_SCANCODE_S) || runtime.isKeyDown(SDL_SCANCODE_DOWN))
        {
            moveDir -= fwd;
        }
        if (runtime.isKeyDown(SDL_SCANCODE_D))
        {
            moveDir += right;
        }
        if (runtime.isKeyDown(SDL_SCANCODE_A))
        {
            moveDir -= right;
        }

        float currentSpeed = moveSpeed;
        if (runtime.isKeyDown(SDL_SCANCODE_LSHIFT) || runtime.isKeyDown(SDL_SCANCODE_RSHIFT))
        {
            currentSpeed *= sprintMultiplier;
        }

        Eigen::Vector2f velocity = Eigen::Vector2f::Zero();
        const float dirNorm = moveDir.norm();
        if (dirNorm > 1e-4f)
        {
            velocity = (moveDir / dirNorm) * currentSpeed;
            bobPhase += dt * 11.0f;
            bobOffset = std::sin(bobPhase) * 3.5f;
        }
        else
        {
            bobOffset *= std::max(0.0f, 1.0f - dt * 10.0f);
        }

        if (body != nullptr)
        {
            body->setVelocity(velocity);
        }

        // Door interaction (E / Return)
        const bool useDown = runtime.isKeyDown(SDL_SCANCODE_E) || runtime.isKeyDown(SDL_SCANCODE_RETURN);
        if (useDown && !useWasDown)
        {
            interactDoor();
        }
        useWasDown = useDown;

        // Weapon firing (Space / F / Ctrl / Mouse Left)
        const bool mouseFireDown = leftMouseDown && !suppressMouseFireUntilRelease && mouseLookActive;
        const bool fireDown = runtime.isKeyDown(SDL_SCANCODE_SPACE) || runtime.isKeyDown(SDL_SCANCODE_F) ||
                              runtime.isKeyDown(SDL_SCANCODE_LCTRL) || mouseFireDown;
        const bool autoFire = (currentWeapon == WEAPON_PLASMA);
        if (fireDown && (autoFire || !fireWasDown || fireCooldownTimer <= 0.0f))
        {
            fireCurrentWeapon();
        }
        fireWasDown = fireDown;
    }

    bool DoomPlayer::fireCurrentWeapon()
    {
        if (isDead() || fireCooldownTimer > 0.0f)
        {
            return false;
        }

        if (currentWeapon == WEAPON_SHOTGUN && (!hasShotgun || ammo < 2))
        {
            currentWeapon = WEAPON_PISTOL;
        }
        else if (currentWeapon == WEAPON_PLASMA && (!hasPlasma || ammo < 1))
        {
            currentWeapon = hasShotgun && ammo >= 2 ? WEAPON_SHOTGUN : WEAPON_PISTOL;
        }

        const Eigen::Vector2f origin = getGameObject()->getPosition();
        const Eigen::Vector2f fwd = getForward();

        if (currentWeapon == WEAPON_SHOTGUN)
        {
            ammo -= 2;
            fireCooldownTimer = 0.62f;
            weaponFireVisualTimer = 0.16f;
            muzzleLight = 1.0f;
            recoil = 1.0f;
            ++shotsFired;
            if (audio && shotgunSound)
            {
                audio->playAndForget(shotgunSound.get());
            }
            constexpr int pelletCount = 7;
            constexpr float spreadHalf = 0.085f;
            for (int i = 0; i < pelletCount; ++i)
            {
                const float t = static_cast<float>(i) / static_cast<float>(pelletCount - 1) * 2.0f - 1.0f;
                const float angle = t * spreadHalf;
                const float c = std::cos(angle);
                const float s = std::sin(angle);
                const Eigen::Vector2f pelletDir(fwd.x() * c - fwd.y() * s, fwd.x() * s + fwd.y() * c);
                resolveHitscanRay(origin, pelletDir, MAX_VIEW_DIST, 12, (i % 2) == 0);
            }
        }
        else if (currentWeapon == WEAPON_PLASMA && plasmaBoltTemplate)
        {
            ammo -= 1;
            fireCooldownTimer = 0.14f;
            weaponFireVisualTimer = 0.10f;
            muzzleLight = 0.75f;
            recoil = 0.45f;
            ++shotsFired;
            if (audio && plasmaSound)
            {
                audio->playAndForget(plasmaSound.get());
            }
            const Eigen::Vector2f spawnPos = origin + fwd * 22.0f;
            if (GameObject *boltObj = platformator::instantiate(plasmaBoltTemplate, spawnPos))
            {
                if (auto *proj = platformator::getBehavior<DoomProjectile>(boltObj))
                {
                    proj->launch(fwd);
                }
            }
        }
        else
        {
            fireCooldownTimer = 0.28f;
            weaponFireVisualTimer = 0.12f;
            muzzleLight = 0.85f;
            recoil = 0.7f;
            ++shotsFired;
            if (audio && pistolSound)
            {
                audio->playAndForget(pistolSound.get());
            }
            resolveHitscanRay(origin, fwd, MAX_VIEW_DIST, 18, true);
        }

        if (auto *game = DoomGameManager::tryGetInstance())
        {
            game->onPlayerStatusChanged();
        }
        return true;
    }

    void DoomPlayer::resolveHitscanRay(const Eigen::Vector2f &origin, const Eigen::Vector2f &dir, float maxDist,
                                         int damage, bool spawnSpark)
    {
        auto &runtime = getRuntime();
        const auto hit = runtime.raycast(origin, dir, maxDist, HITSCAN_RAY_MASK, false);
        if (!hit.has_value())
        {
            return;
        }
        if (spawnSpark && hitSparkTemplate)
        {
            // Pull impact spark slightly toward the viewer so its depth sits cleanly in front of walls.
            const Eigen::Vector2f sparkPos = hit->point - dir * 3.0f;
            platformator::instantiate(hitSparkTemplate, sparkPos);
        }
        if (hit->gameObject == nullptr)
        {
            return;
        }
        if (auto *enemy = platformator::getBehavior<DoomEnemy>(hit->gameObject))
        {
            enemy->takeDamage(damage);
        }
        else if (auto *barrel = platformator::getBehavior<DoomBarrel>(hit->gameObject))
        {
            barrel->takeDamage(damage);
        }
    }

    bool DoomPlayer::interactDoor()
    {
        const Eigen::Vector2f origin = getGameObject()->getPosition();
        const Eigen::Vector2f fwd = getForward();
        const auto hit = getRuntime().raycast(origin, fwd, 78.0f, WALL_RAY_MASK, false);
        if (!hit.has_value() || hit->gameObject == nullptr)
        {
            return false;
        }
        if (auto *door = platformator::getBehavior<DoomDoor>(hit->gameObject))
        {
            auto *game = DoomGameManager::tryGetInstance();
            return door->tryActivate(game ? game->hasRedKey() : false);
        }
        return false;
    }

    void DoomPlayer::respawn()
    {
        hp = maxHp;
        armor = std::max(armor, 25);
        ammo = std::max(ammo, 20);
        setYaw(startYaw);
        getGameObject()->setPosition(spawnPosition);
        if (body != nullptr)
        {
            body->setVelocity(Eigen::Vector2f::Zero());
        }
        if (auto *game = DoomGameManager::tryGetInstance())
        {
            game->triggerScreenFlash(120, 220, 255, 90, 0.25f);
            game->onPlayerStatusChanged();
        }
    }

    bool DoomPlayer::selectWeapon(int weaponKind)
    {
        if (!hasWeapon(weaponKind))
        {
            return false;
        }
        currentWeapon = weaponKind;
        return true;
    }

    void DoomPlayer::giveWeapon(int weaponKind)
    {
        if (weaponKind == WEAPON_SHOTGUN)
        {
            hasShotgun = true;
        }
        else if (weaponKind == WEAPON_PLASMA)
        {
            hasPlasma = true;
        }
    }

    bool DoomPlayer::hasWeapon(int weaponKind) const
    {
        switch (weaponKind)
        {
        case WEAPON_PISTOL:
            return true;
        case WEAPON_SHOTGUN:
            return hasShotgun;
        case WEAPON_PLASMA:
            return hasPlasma;
        default:
            return false;
        }
    }

    int DoomPlayer::getCurrentWeapon() const
    {
        return currentWeapon;
    }

    bool DoomPlayer::addHealth(int amount)
    {
        if (hp >= maxHp || amount <= 0)
        {
            return false;
        }
        hp = std::min(maxHp, hp + amount);
        if (auto *game = DoomGameManager::tryGetInstance())
        {
            game->onPlayerStatusChanged();
        }
        return true;
    }

    bool DoomPlayer::addArmor(int amount)
    {
        if (armor >= 100 || amount <= 0)
        {
            return false;
        }
        armor = std::min(100, armor + amount);
        if (auto *game = DoomGameManager::tryGetInstance())
        {
            game->onPlayerStatusChanged();
        }
        return true;
    }

    bool DoomPlayer::addAmmo(int amount)
    {
        if (ammo >= maxAmmo || amount <= 0)
        {
            return false;
        }
        ammo = std::min(maxAmmo, ammo + amount);
        if (auto *game = DoomGameManager::tryGetInstance())
        {
            game->onPlayerStatusChanged();
        }
        return true;
    }

    void DoomPlayer::takeDamage(int damage)
    {
        if (isDead() || damage <= 0 || hurtCooldown > 0.0f)
        {
            return;
        }
        hurtCooldown = 0.08f;

        int absorbed = 0;
        if (armor > 0)
        {
            absorbed = std::min(armor, damage / 2);
            armor -= absorbed;
        }
        const int direct = damage - absorbed;
        hp = std::max(0, hp - direct);

        if (audio && hurtSound)
        {
            audio->playAndForget(hurtSound.get());
        }
        if (auto *game = DoomGameManager::tryGetInstance())
        {
            game->triggerScreenFlash(220, 20, 20, 140, 0.30f);
            game->onPlayerStatusChanged();
        }
    }

    float DoomPlayer::getYaw() const
    {
        return yaw;
    }

    void DoomPlayer::setYaw(float newYaw)
    {
        if (auto *object = getGameObject())
        {
            object->setRotation(newYaw);
        }
        yaw = newYaw;
    }

    Eigen::Vector2f DoomPlayer::getForward() const
    {
        return Eigen::Vector2f(std::cos(yaw), std::sin(yaw));
    }

    Eigen::Vector2f DoomPlayer::getRight() const
    {
        return Eigen::Vector2f(-std::sin(yaw), std::cos(yaw));
    }

    int DoomPlayer::getHp() const
    {
        return hp;
    }

    int DoomPlayer::getMaxHp() const
    {
        return maxHp;
    }

    int DoomPlayer::getArmor() const
    {
        return armor;
    }

    int DoomPlayer::getAmmo() const
    {
        return ammo;
    }

    int DoomPlayer::getMaxAmmo() const
    {
        return maxAmmo;
    }

    bool DoomPlayer::isDead() const
    {
        return hp <= 0;
    }

    float DoomPlayer::getBobOffset() const
    {
        return bobOffset;
    }

    float DoomPlayer::getWeaponFireProgress() const
    {
        return weaponFireVisualTimer;
    }

    bool DoomPlayer::isWeaponFiring() const
    {
        return weaponFireVisualTimer > 0.0f;
    }

    float DoomPlayer::getMuzzleLight() const
    {
        return muzzleLight;
    }

    float DoomPlayer::getRecoil() const
    {
        return recoil;
    }

    int DoomPlayer::getShotsFired() const
    {
        return shotsFired;
    }

    void DoomPlayer::applyMouseLock(bool enabled)
    {
        mouseLookPreferred = enabled;
        SDL_Window *win = getRuntime().getWindowHandle();
        const bool interactiveWindow =
            (win != nullptr) && ((SDL_GetWindowFlags(win) & SDL_WINDOW_HIDDEN) == 0);
        if (!interactiveWindow)
        {
            mouseLookActive = false;
            mouseLockSyncPending = false;
        }
        else
        {
            const bool ok = SDL_SetWindowRelativeMouseMode(win, enabled);
            mouseLookActive = enabled;
            mouseLockSyncPending = enabled && !ok;
            if (enabled)
            {
                float dummyX = 0.0f, dummyY = 0.0f;
                SDL_GetRelativeMouseState(&dummyX, &dummyY);
            }
        }
        if (auto *game = DoomGameManager::tryGetInstance())
        {
            game->onPlayerStatusChanged();
        }
    }

    void DoomPlayer::setMouseLock(bool enabled)
    {
        applyMouseLock(enabled);
    }

    bool DoomPlayer::isMouseLookActive() const
    {
        return mouseLookActive;
    }

    bool DoomPlayer::isMouseLookPreferred()
    {
        return mouseLookPreferred;
    }

    void DoomPlayer::setMouseLookPreferred(bool enabled)
    {
        mouseLookPreferred = enabled;
    }

    void DoomPlayer::setExternalMoveInput(const Eigen::Vector2f &moveDir)
    {
        externalMoveInput = moveDir;
    }
} // namespace doom
