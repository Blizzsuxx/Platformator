#pragma once

#include "behaviorfactoryregistry.h"
#include "jsonhelpers.h"
#include "behavior.h"
#include "assetreference.h"
#include "objectreference.h"
#include "animator.h"
#include "collider.h"
#include "rigidbody.h"
#include "sprite.h"
#include "helpers.h"
#include "audio.h"
#include "audiowrapper.h"

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
        AssetReference<AnimationClip> walkAnimset;
        AssetReference<AnimationClip> squashAnimset;
        AssetReference<AudioWrapper> stompSound;
        bool defeated;
        double defeatedTimer;

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