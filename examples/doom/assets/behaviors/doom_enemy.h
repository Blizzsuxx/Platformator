#pragma once

#include "doom_constants.h"
#include "platformator/animator.h"
#include "platformator/assetreference.h"
#include "platformator/audio.h"
#include "platformator/behavior.h"
#include "platformator/rigidbody.h"
#include "platformator/scriptregistration.h"
#include "platformator/sprite.h"

namespace doom
{
    class DoomEnemy : public Behavior
    {
      public:
        DoomEnemy();
        ~DoomEnemy() override;

        void start() override;
        void fixedUpdate(double timeDelta) override;
        void update(double timeDelta) override;
        void onDestroy() override;

        void takeDamage(int damage);
        int getHp() const;
        int getMaxHp() const;
        bool isDefeated() const;
        bool isAlerted() const;

      private:
        int enemyKind = ENEMY_TROOPER;
        int hp = 40;
        int maxHp = 40;
        float moveSpeed = 75.0f;
        float detectRange = 520.0f;
        float attackRange = 360.0f;
        float attackCooldown = 1.25f;
        int attackDamage = 10;
        platformator::TemplateAssetRef fireballTemplate;
        platformator::TemplateAssetRef dropTemplate;
        platformator::TemplateAssetRef deathFxTemplate;
        platformator::AnimationClipRef walkAnim;
        platformator::AnimationClipRef attackAnim;
        platformator::AnimationClipRef hurtAnim;
        platformator::AudioAssetRef alertSound;
        platformator::AudioAssetRef attackSound;
        platformator::AudioAssetRef hurtSound;
        platformator::AudioAssetRef deathSound;

        Rigidbody *body = nullptr;
        Animator *animator = nullptr;
        Sprite *sprite = nullptr;
        Audio *audio = nullptr;

        bool registered = false;
        bool alerted = false;
        bool defeated = false;
        float attackTimer = 0.4f;
        float hurtTimer = 0.0f;
        float animHoldTimer = 0.0f;
        float strafeSign = 1.0f;
        float strafeTimer = 0.0f;

        void ensureRegistered();
        void performAttack(const Eigen::Vector2f &dirToPlayer);
        void updateAnimationState();

        SERIALIZABLE_SCRIPT(
            DoomEnemy,
            enemyKind,
            hp,
            maxHp,
            moveSpeed,
            detectRange,
            attackRange,
            attackCooldown,
            attackDamage,
            fireballTemplate,
            dropTemplate,
            deathFxTemplate,
            walkAnim,
            attackAnim,
            hurtAnim,
            alertSound,
            attackSound,
            hurtSound,
            deathSound);
    };
} // namespace doom
