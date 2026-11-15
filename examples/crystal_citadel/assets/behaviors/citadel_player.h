#pragma once

#include <SDL3/SDL.h>

#include "citadel_constants.h"
#include "platformator/animator.h"
#include "platformator/assetreference.h"
#include "platformator/audio.h"
#include "platformator/behavior.h"
#include "platformator/rigidbody.h"
#include "platformator/scriptregistration.h"
#include "platformator/sprite.h"

namespace citadel
{
    class CitadelPlayer : public Behavior
    {
      public:
        CitadelPlayer();

        void start() override;
        void fixedUpdate(double timeDelta) override;
        void update(double timeDelta) override;
        void onCollisionEnter(const Collision *collision, Collider *other, double timeDelta) override;

        bool fireBolt();
        bool grantOrbitShield();
        void heal(int amount);
        void takeDamage(int amount);
        void bounce(float factor);
        void respawn();

        int getHp() const;
        int getMaxHp() const;
        int getBoltsFired() const;
        float getFacing() const;
        bool hasActiveShield() const;
        Rigidbody *getBody() const;

      private:
        float walkSpeed = PLAYER_WALK_SPEED;
        float jumpSpeed = PLAYER_JUMP_SPEED;
        float gravity = PLAYER_GRAVITY;
        int maxHp = 5;
        int hp = 5;
        float fireCooldown = 0.28f;
        platformator::TemplateAssetRef boltTemplate;
        platformator::TemplateAssetRef jumpFxTemplate;
        platformator::TemplateAssetRef shieldTemplate;
        platformator::AnimationClipRef idleAnim;
        platformator::AnimationClipRef runAnim;
        platformator::AnimationClipRef jumpAnim;
        platformator::AnimationClipRef fallAnim;
        platformator::AudioAssetRef jumpSound;
        platformator::AudioAssetRef shootSound;
        platformator::AudioAssetRef hurtSound;

        Rigidbody *body = nullptr;
        Animator *animator = nullptr;
        Sprite *sprite = nullptr;
        Audio *audio = nullptr;
        Eigen::Vector2f spawnPosition = Eigen::Vector2f::Zero();
        float maxFallSpeed = PLAYER_MAX_FALL_SPEED;
        float facing = 1.0f;
        double fireTimer = 0.0;
        double invulnerableTimer = 0.0;
        int airJumpsRemaining = 1;
        int boltsFired = 0;
        bool jumpWasPressed = false;
        bool fireWasPressed = false;
        bool respawnWasPressed = false;

        void updateInput(double timeDelta);
        void updateAnimation();
        void spawnJumpBurst();

        SERIALIZABLE_SCRIPT(
            CitadelPlayer,
            walkSpeed,
            jumpSpeed,
            gravity,
            maxHp,
            hp,
            fireCooldown,
            boltTemplate,
            jumpFxTemplate,
            shieldTemplate,
            idleAnim,
            runAnim,
            jumpAnim,
            fallAnim,
            jumpSound,
            shootSound,
            hurtSound);
    };
} // namespace citadel
