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

        std::string getTypeName() const override;
        BEHAVIOR_FIELDS(
            MarioPlayer,
            BEHAVIOR_FIELD(walkSpeed),
            BEHAVIOR_FIELD(jumpSpeed),
            BEHAVIOR_FIELD(gravity),
            BEHAVIOR_FIELD(maxFallSpeed),
            BEHAVIOR_FIELD(stompBounceFactor));

        void start() override;
        void fixedUpdate(double timeDelta) override;
        void update(double timeDelta) override;
        void onCollisionEnter(Collider *other, double timeDelta) override;

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