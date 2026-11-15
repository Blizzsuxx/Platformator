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
    class CitadelSplittingSlime : public Behavior
    {
      public:
        CitadelSplittingSlime() = default;

        void start() override;
        void fixedUpdate(double timeDelta) override;
        void onCollisionEnter(const Collision *collision, Collider *other, double timeDelta) override;

        void configureSpawn(int newGeneration, float newDirection, float newWalkSpeed,
                            float newMinX, float newMaxX, int newHp);
        void takeDamage(int amount);
        int getGeneration() const;
        bool isDefeated() const;

      private:
        int generation = 1;
        int maxGeneration = 2;
        int hp = 1;
        float walkSpeed = SLIME_WALK_SPEED;
        float minX = 0.0f;
        float maxX = 0.0f;
        float direction = -1.0f;
        platformator::TemplateAssetRef selfTemplate;
        platformator::TemplateAssetRef gemTemplate;
        platformator::TemplateAssetRef splitFxTemplate;
        platformator::AnimationClipRef bounceAnim;
        platformator::AudioAssetRef splitSound;

        Rigidbody *body = nullptr;
        Animator *animator = nullptr;
        Sprite *sprite = nullptr;
        Audio *audio = nullptr;
        double hopTimer = 0.8;
        bool defeated = false;

        void splitAndDefeat();

        SERIALIZABLE_SCRIPT(
            CitadelSplittingSlime,
            generation,
            maxGeneration,
            hp,
            walkSpeed,
            minX,
            maxX,
            direction,
            selfTemplate,
            gemTemplate,
            splitFxTemplate,
            bounceAnim,
            splitSound);
    };
} // namespace citadel
