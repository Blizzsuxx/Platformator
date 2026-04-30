#pragma once

#include <SDL3/SDL.h>

#include "behaviorfactoryregistry.h"
#include "mario_entity.h"

class Animator;
class Collider;
class Rigidbody;
class Sprite;

namespace mario
{
    class MarioPlayer : public MarioEntity
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
        float walkSpeed;
        float jumpSpeed;
        float gravity;
        float maxFallSpeed;
        float stompBounceFactor;
        AnimationClip idleAnimset;
        AnimationClip runAnimset;
        AnimationClip jumpAnimset;
        AnimationClip fallAnimset;
        AnimationClip winAnimset;
        AudioWrapper jumpSound;
        AudioWrapper hurtSound;
        Eigen::Vector2f spawn;
        Eigen::Vector2f positionBeforePhysics;
        Eigen::Vector2f velocityBeforePhysics;
        bool jumpWasPressed;
        bool respawnWasPressed;

        void respawn();
        void updateInput(double timeDelta);
        void updateAnimation();
    };

    REGISTER_SCRIPT(
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
} // namespace mario