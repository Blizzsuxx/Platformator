#pragma once

#include <Eigen/Dense>
#include <SDL3/SDL.h>

#include "doom_constants.h"
#include "platformator/assetreference.h"
#include "platformator/audio.h"
#include "platformator/behavior.h"
#include "platformator/rigidbody.h"
#include "platformator/scriptregistration.h"

namespace doom
{
    class DoomPlayer : public Behavior
    {
      public:
        DoomPlayer();

        void start() override;
        void fixedUpdate(double timeDelta) override;

        bool fireCurrentWeapon();
        bool interactDoor();
        void respawn();

        bool selectWeapon(int weaponKind);
        void giveWeapon(int weaponKind);
        bool hasWeapon(int weaponKind) const;
        int getCurrentWeapon() const;

        bool addHealth(int amount);
        bool addArmor(int amount);
        bool addAmmo(int amount);
        void takeDamage(int damage);

        float getYaw() const;
        void setYaw(float newYaw);
        Eigen::Vector2f getForward() const;
        Eigen::Vector2f getRight() const;

        int getHp() const;
        int getMaxHp() const;
        int getArmor() const;
        int getAmmo() const;
        int getMaxAmmo() const;
        bool isDead() const;

        float getBobOffset() const;
        float getWeaponFireProgress() const;
        bool isWeaponFiring() const;
        float getMuzzleLight() const;
        float getRecoil() const;
        int getShotsFired() const;

        void setMouseLock(bool enabled);
        bool isMouseLookActive() const;
        static bool isMouseLookPreferred();
        static void setMouseLookPreferred(bool enabled);
        void setExternalMoveInput(const Eigen::Vector2f &moveDir);

      private:
        float moveSpeed = 165.0f;
        float sprintMultiplier = 1.45f;
        float turnSpeed = 2.4f;
        float startYaw = 0.0f;
        int maxHp = 100;
        int hp = 100;
        int armor = 25;
        int ammo = 28;
        int maxAmmo = 100;
        int currentWeapon = WEAPON_PISTOL;
        bool hasShotgun = false;
        bool hasPlasma = false;

        platformator::TemplateAssetRef plasmaBoltTemplate;
        platformator::TemplateAssetRef hitSparkTemplate;
        platformator::AudioAssetRef pistolSound;
        platformator::AudioAssetRef shotgunSound;
        platformator::AudioAssetRef plasmaSound;
        platformator::AudioAssetRef hurtSound;
        platformator::AudioAssetRef emptySound;

        Rigidbody *body = nullptr;
        Audio *audio = nullptr;
        Eigen::Vector2f spawnPosition = Eigen::Vector2f::Zero();
        float yaw = 0.0f;
        float bobPhase = 0.0f;
        float bobOffset = 0.0f;
        float fireCooldownTimer = 0.0f;
        float weaponFireVisualTimer = 0.0f;
        float muzzleLight = 0.0f;
        float recoil = 0.0f;
        float hurtCooldown = 0.0f;
        int shotsFired = 0;

        bool fireWasDown = false;
        bool useWasDown = false;
        bool mapWasDown = false;
        static bool mouseLookPreferred;
        bool mouseLookActive = false;
        bool mouseLockSyncPending = false;
        bool mouseToggleWasDown = false;
        bool escapeWasDown = false;
        bool leftMouseWasDown = false;
        bool suppressMouseFireUntilRelease = false;
        bool respawnWasDown = false;
        Eigen::Vector2f externalMoveInput = Eigen::Vector2f::Zero();

        void applyMouseLock(bool enabled);
        void resolveHitscanRay(const Eigen::Vector2f &origin, const Eigen::Vector2f &dir, float maxDist,
                               int damage, bool spawnSpark);

        SERIALIZABLE_SCRIPT(
            DoomPlayer,
            moveSpeed,
            sprintMultiplier,
            turnSpeed,
            startYaw,
            maxHp,
            hp,
            armor,
            ammo,
            maxAmmo,
            currentWeapon,
            hasShotgun,
            hasPlasma,
            plasmaBoltTemplate,
            hitSparkTemplate,
            pistolSound,
            shotgunSound,
            plasmaSound,
            hurtSound,
            emptySound);
    };
} // namespace doom
