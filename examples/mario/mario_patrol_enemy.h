#pragma once

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

        std::string getTypeName() const override;
        void deserialize(const ScriptDescriptor &descriptor) override;
        void serialize(ScriptDescriptor &descriptor) const override;

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
        bool defeated;
        double defeatedTimer;

        void awake() override;
    };
} // namespace mario