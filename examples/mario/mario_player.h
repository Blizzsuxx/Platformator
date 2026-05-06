#pragma once

#include <SDL3/SDL.h>

#include "platformator/assetreference.h"
#include "platformator/audio.h"
#include "platformator/animator.h"
#include "platformator/behavior.h"
#include "platformator/rigidbody.h"
#include "platformator/scriptregistration.h"
#include "platformator/sprite.h"

namespace mario
{
    class MarioPlayer : public Behavior
    {
    public:
        MarioPlayer();

        void start() override;
        void fixedUpdate(double timeDelta) override;
        void update(double timeDelta) override;
        void onCollisionEnter(const Collision *collision, Collider *other, double timeDelta) override;

        void defeat();
        void bounceAfterStomp();
        void stopForWin();

        Rigidbody *getBody() const;
        const Eigen::Vector2f &getPositionBeforePhysics() const;
        const Eigen::Vector2f &getVelocityBeforePhysics() const;

    private:
        Rigidbody *body;
        Animator *animator;
        Sprite *sprite;
        Audio *audio;
        float walkSpeed;
        float jumpSpeed;
        float gravity;
        float maxFallSpeed;
        float stompBounceFactor;
        platformator::AnimationClipRef idleAnimset;
        platformator::AnimationClipRef runAnimset;
        platformator::AnimationClipRef jumpAnimset;
        platformator::AnimationClipRef fallAnimset;
        platformator::AnimationClipRef winAnimset;
        platformator::AudioAssetRef jumpSound;
        platformator::AudioAssetRef hurtSound;
        Eigen::Vector2f spawn;
        bool jumpWasPressed;
        bool respawnWasPressed;

        void respawn();
        void updateInput(double timeDelta);
        void updateAnimation();

        SERIALIZABLE_SCRIPT(
            MarioPlayer,
            walkSpeed,
            jumpSpeed,
            gravity,
            maxFallSpeed,
            stompBounceFactor,
            idleAnimset,
            runAnimset,
            jumpAnimset,
            fallAnimset,
            winAnimset,
            jumpSound,
            hurtSound);
    };
} // namespace mario