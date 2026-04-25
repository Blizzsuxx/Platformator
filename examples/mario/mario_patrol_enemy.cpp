#include "mario_patrol_enemy.h"

#include "animator.h"
#include "collider.h"
#include "mario_constants.h"
#include "mario_game.h"
#include "mario_helpers.h"
#include "mario_player.h"
#include "rigidbody.h"
#include "sprite.h"

namespace mario
{
    MarioPatrolEnemy::MarioPatrolEnemy()
        : body(nullptr),
          collider(nullptr),
          animator(nullptr),
          sprite(nullptr),
          minX(0.0f),
          maxX(0.0f),
          direction(-1.0f),
          walkSpeed(ENEMY_WALK_SPEED),
          stompMinSpeed(10.0f),
          stompTolerance(STOMP_TOLERANCE),
          squashDuration(static_cast<float>(ENEMY_SQUASH_DURATION)),
          defeated(false),
          defeatedTimer(0.0)
    {
    }

    std::string MarioPatrolEnemy::getTypeName() const
    {
        return "MarioPatrolEnemy";
    }

    void MarioPatrolEnemy::deserialize(const ScriptDescriptor &descriptor)
    {
        minX = descriptor.getFloat("minx", minX);
        maxX = descriptor.getFloat("maxx", maxX);
        direction = descriptor.getFloat("direction", direction);
        walkSpeed = descriptor.getFloat("walkspeed", walkSpeed);
        stompMinSpeed = descriptor.getFloat("stompminspeed", stompMinSpeed);
        stompTolerance = descriptor.getFloat("stomptolerance", stompTolerance);
        squashDuration = descriptor.getFloat("squashduration", squashDuration);
    }

    void MarioPatrolEnemy::serialize(ScriptDescriptor &descriptor) const
    {
        descriptor.setFloatProperty("minx", minX);
        descriptor.setFloatProperty("maxx", maxX);
        descriptor.setFloatProperty("direction", direction);
        descriptor.setFloatProperty("walkspeed", walkSpeed);
        descriptor.setFloatProperty("stompminspeed", stompMinSpeed);
        descriptor.setFloatProperty("stomptolerance", stompTolerance);
        descriptor.setFloatProperty("squashduration", squashDuration);
    }

    void MarioPatrolEnemy::start()
    {
        body = getGameObject()->getComponent<Rigidbody>();
        collider = getGameObject()->getComponent<Collider>();
        animator = getGameObject()->getComponent<Animator>();
        sprite = getGameObject()->getComponent<Sprite>();

        if (maxX <= minX)
        {
            float spawnX = getGameObject()->getX();
            minX = spawnX - 90.0f;
            maxX = spawnX + 90.0f;
        }
    }

    void MarioPatrolEnemy::fixedUpdate(double timeDelta)
    {
        if (!isActive())
        {
            return;
        }

        if (defeated)
        {
            defeatedTimer -= timeDelta;
            if (defeatedTimer <= 0.0)
            {
                getGameObject()->setActive(false);
            }
            return;
        }

        if (getGameObject()->getX() <= minX)
        {
            direction = 1.0f;
        }
        else if (getGameObject()->getX() >= maxX)
        {
            direction = -1.0f;
        }

        Eigen::Vector2f velocity = body->getVelocity();
        velocity.x() = direction * walkSpeed;
        body->setVelocity(velocity);

        if (sprite != nullptr)
        {
            sprite->setFlip(direction < 0.0f ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
        }

        playClipIfChanged(animator, "walk");
    }

    void MarioPatrolEnemy::handlePlayerContact(MarioPlayer &player)
    {
        MarioGame &game = MarioGame::getInstance();
        if (game.isGameWon() || !isActive() || defeated)
        {
            return;
        }

        const Bounds playerBounds = getBounds(player.getGameObject());
        const Bounds enemyBounds = getBounds(getGameObject());
        const float previousPlayerBottom = player.getPositionBeforePhysics().y() + playerBounds.halfExtents.y();
        const float enemyTop = enemyBounds.center.y() - enemyBounds.halfExtents.y();
        const bool stomped = player.getVelocityBeforePhysics().y() > stompMinSpeed && previousPlayerBottom <= enemyTop + stompTolerance;

        if (!stomped)
        {
            player.defeat();
            return;
        }

        defeated = true;
        defeatedTimer = squashDuration;

        if (collider != nullptr)
        {
            collider->setCollisionMask(0);
        }

        if (body != nullptr)
        {
            body->setVelocity(Eigen::Vector2f::Zero());
        }

        if (animator != nullptr)
        {
            playClipIfChanged(animator, "squash");
        }
        else
        {
            getGameObject()->setActive(false);
            defeatedTimer = 0.0;
        }

        player.bounceAfterStomp();
        game.playStompSound();
    }
} // namespace mario