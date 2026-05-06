#pragma once

#include "platformator/animator.h"
#include "platformator/assetreference.h"
#include "platformator/audio.h"
#include "platformator/behavior.h"
#include "platformator/collider.h"
#include "platformator/rigidbody.h"
#include "platformator/scriptregistration.h"
#include "platformator/sprite.h"

class Animator;
class Collider;
class Rigidbody;
class Sprite;

namespace mario
{
    class MarioPlayer;

    class MarioPatrolEnemy : public Behavior
    {
    public:
        MarioPatrolEnemy();

        void start() override;
        void fixedUpdate(double timeDelta) override;
        void onCollisionEnter(const Collision *collision, Collider *other, double timeDelta) override;
        void handlePlayerContact(MarioPlayer *player, const Collision *collision);

    private:
        Rigidbody *body;
        Collider *collider;
        Animator *animator;
        Sprite *sprite;
        Audio *audio;
        float direction;
        float walkSpeed;
        float squashDuration;
        platformator::AnimationClipRef walkAnimset;
        platformator::AnimationClipRef squashAnimset;
        platformator::AudioAssetRef stompSound;
        bool defeated;
        double defeatedTimer;
        float maxFallSpeed;

        SERIALIZABLE_SCRIPT(
            MarioPatrolEnemy,
            direction,
            walkSpeed,
            squashDuration,
            walkAnimset,
            squashAnimset,
            stompSound);
    };
} // namespace mario