#pragma once

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
    class CitadelPatrolEnemy : public Behavior
    {
      public:
        CitadelPatrolEnemy() = default;

        void start() override;
        void fixedUpdate(double timeDelta) override;
        void onCollisionEnter(const Collision *collision, Collider *other, double timeDelta) override;

        void configurePatrol(float newMinX, float newMaxX, float newDirection);
        void takeDamage(int amount);
        bool isDefeated() const;

      private:
        float walkSpeed = ENEMY_WALK_SPEED;
        float minX = 0.0f;
        float maxX = 0.0f;
        float direction = -1.0f;
        int hp = 2;
        platformator::TemplateAssetRef dropTemplate;
        platformator::TemplateAssetRef deathFxTemplate;
        platformator::AnimationClipRef walkAnim;
        platformator::AnimationClipRef squashAnim;
        platformator::AudioAssetRef hitSound;

        Rigidbody *body = nullptr;
        Animator *animator = nullptr;
        Sprite *sprite = nullptr;
        Audio *audio = nullptr;
        bool defeated = false;
        double defeatedTimer = 0.0;

        void applyDirection();

        SERIALIZABLE_SCRIPT(
            CitadelPatrolEnemy,
            walkSpeed,
            minX,
            maxX,
            direction,
            hp,
            dropTemplate,
            deathFxTemplate,
            walkAnim,
            squashAnim,
            hitSound);
    };
} // namespace citadel
