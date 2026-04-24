#pragma once

#include "mario_entity.h"

class Animator;
class Collider;
class Rigidbody;
class Sprite;

namespace mario
{
    class MarioGame;
    class MarioPlayer;

    class MarioPatrolEnemy : public MarioEntity
    {
    public:
        MarioPatrolEnemy(GameObject *gameObject, float minX, float maxX, float direction);

        void update(double timeDelta) override;
        void handlePlayerContact(MarioGame &game, MarioPlayer &player);

    private:
        Rigidbody *body;
        Collider *collider;
        Animator *animator;
        Sprite *sprite;
        float minX;
        float maxX;
        float direction;
        bool defeated;
        double defeatedTimer;
    };
} // namespace mario