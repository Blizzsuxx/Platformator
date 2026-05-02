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
        AssetReference<AnimationClip> walkAnimset;
        AssetReference<AnimationClip> squashAnimset;
        AssetReference<AudioWrapper> stompSound;
        bool defeated;
        double defeatedTimer;

        SERIALIZABLE_SCRIPT(
            MarioPatrolEnemy,
            minX,
            maxX,
            direction,
            walkSpeed,
            stompMinSpeed,
            stompTolerance,
            squashDuration,
            walkAnimset,
            squashAnimset,
            stompSound);
    };
} // namespace mario