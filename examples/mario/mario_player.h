#pragma once

#include <SDL3/SDL.h>

#include "mario_entity.h"

class Animator;
class Collider;
class Rigidbody;
class Sprite;

namespace mario
{
    class MarioGame;

    class MarioPlayer : public MarioEntity
    {
    public:
        explicit MarioPlayer(GameObject *gameObject);

        void registerCallbacks(MarioGame &game);
        void update(MarioGame &game, double timeDelta);
        void handleKeyDown(SDL_Keycode key);
        void respawn();
        void defeat(MarioGame &game);
        void bounceAfterStomp();
        void stopForWin();

        Rigidbody *getBody() const;
        const Eigen::Vector2f &getPositionBeforePhysics() const;
        const Eigen::Vector2f &getVelocityBeforePhysics() const;

    private:
        Rigidbody *body;
        Collider *collider;
        Animator *animator;
        Sprite *sprite;
        Eigen::Vector2f spawn;
        Eigen::Vector2f positionBeforePhysics;
        Eigen::Vector2f velocityBeforePhysics;
        bool jumpWasPressed;

        void handleCollisionEnter(MarioGame &game, Collider *other);
        void updateInput(MarioGame &game, double timeDelta);
        void updateAnimation(const MarioGame &game);
    };
} // namespace mario