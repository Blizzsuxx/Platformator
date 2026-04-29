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

        BEHAVIOR_FIELDS(
            MarioPlayer,
            BEHAVIOR_FIELD(walkSpeed),
            BEHAVIOR_FIELD(jumpSpeed),
            BEHAVIOR_FIELD(gravity),
            BEHAVIOR_FIELD(maxFallSpeed),
            BEHAVIOR_FIELD(stompBounceFactor),
            BEHAVIOR_FIELD(idleAnimset),
            BEHAVIOR_FIELD(runAnimset),
            BEHAVIOR_FIELD(jumpAnimset),
            BEHAVIOR_FIELD(fallAnimset),
            BEHAVIOR_FIELD(winAnimset),
            BEHAVIOR_FIELD(jumpSound),
            BEHAVIOR_FIELD(hurtSound));

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
        platformator_behavior_detail::AnimationClipReference idleAnimset;
        platformator_behavior_detail::AnimationClipReference runAnimset;
        platformator_behavior_detail::AnimationClipReference jumpAnimset;
        platformator_behavior_detail::AnimationClipReference fallAnimset;
        platformator_behavior_detail::AnimationClipReference winAnimset;
        platformator_behavior_detail::AudioAssetReference jumpSound;
        platformator_behavior_detail::AudioAssetReference hurtSound;
        Eigen::Vector2f spawn;
        Eigen::Vector2f positionBeforePhysics;
        Eigen::Vector2f velocityBeforePhysics;
        bool jumpWasPressed;
        bool respawnWasPressed;

        void respawn();
        void updateInput(double timeDelta);
        void updateAnimation();
    };

    REGISTER_BEHAVIOR(MarioPlayer);
} // namespace mario