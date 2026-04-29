#pragma once

#include "behaviorfactoryregistry.h"
#include "mario_entity.h"

class Animator;
class Collider;
class Rigidbody;
class Sprite;

namespace mario
{
    class MarioPlayer;

    class MarioPatrolEnemy : public MarioEntity
    {
    public:
        MarioPatrolEnemy();

        BEHAVIOR_FIELDS(
            MarioPatrolEnemy,
            BEHAVIOR_FIELD(minX),
            BEHAVIOR_FIELD(maxX),
            BEHAVIOR_FIELD(direction),
            BEHAVIOR_FIELD(walkSpeed),
            BEHAVIOR_FIELD(stompMinSpeed),
            BEHAVIOR_FIELD(stompTolerance),
            BEHAVIOR_FIELD(squashDuration),
            BEHAVIOR_FIELD(walkAnimset),
            BEHAVIOR_FIELD(squashAnimset),
            BEHAVIOR_FIELD(stompSound));

        void start() override;
        void fixedUpdate(double timeDelta) override;
        void handlePlayerContact(MarioPlayer &player);

    private:
        Rigidbody *body;
        Collider *collider;
        Animator *animator;
        Sprite *sprite;
        float minX;
        float maxX;
        float direction;
        float walkSpeed;
        float stompMinSpeed;
        float stompTolerance;
        float squashDuration;
        platformator_behavior_detail::AnimationClipReference walkAnimset;
        platformator_behavior_detail::AnimationClipReference squashAnimset;
        platformator_behavior_detail::AudioAssetReference stompSound;
        bool defeated;
        double defeatedTimer;
    };

    REGISTER_BEHAVIOR(MarioPatrolEnemy);
} // namespace mario