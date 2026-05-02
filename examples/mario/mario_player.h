#pragma once

#include <SDL3/SDL.h>

#include "behaviorfactoryregistry.h"
#include "jsonhelpers.h"
#include "behavior.h"
#include "assetreference.h"
#include "objectreference.h"
#include "sprite.h"
#include "animator.h"
#include "audio.h"
#include "rigidbody.h"

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
        AssetReference<AnimationClip> idleAnimset;
        AssetReference<AnimationClip> runAnimset;
        AssetReference<AnimationClip> jumpAnimset;
        AssetReference<AnimationClip> fallAnimset;
        AssetReference<AnimationClip> winAnimset;
        AssetReference<AudioWrapper> jumpSound;
        AssetReference<AudioWrapper> hurtSound;
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