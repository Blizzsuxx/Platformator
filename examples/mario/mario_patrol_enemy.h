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
        AnimationClip walkAnimset;
        AnimationClip squashAnimset;
        AudioWrapper stompSound;
        bool defeated;
        double defeatedTimer;
    };

    REGISTER_SCRIPT(
        MarioPatrolEnemy,
        minX,
        maxX,
        walkSpeed,
        stompMinSpeed,
        stompTolerance,
        squashDuration,
        walkAnimset,
        squashAnimset,
        stompSound);
} // namespace mario