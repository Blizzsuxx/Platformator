#include "mario_patrol_enemy.h"

#include "animator.h"
#include "collider.h"
#include "mario_constants.h"
#include "mario_game.h"
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
          direction(-1.0f),
          walkSpeed(ENEMY_WALK_SPEED),
          squashDuration(static_cast<float>(ENEMY_SQUASH_DURATION)),
          walkAnimset(),
          squashAnimset(),
          stompSound(),
          audio(nullptr),
          defeated(false),
          defeatedTimer(0.0)
    {
    }

    void MarioPatrolEnemy::start()
    {
        body = getGameObject()->getComponent<Rigidbody>();
        collider = getGameObject()->getComponent<Collider>();
        animator = getGameObject()->getComponent<Animator>();
        sprite = getGameObject()->getComponent<Sprite>();
        Eigen::Vector2f velocity = body->getVelocity();
        velocity.x() = direction * walkSpeed;
        body->setVelocity(velocity);
        sprite->setFlip(direction < 0.0f ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
        animator->play(walkAnimset.get());
        audio = getGameObject()->getComponent<Audio>();
    }

    void MarioPatrolEnemy::fixedUpdate(double timeDelta)
    {
        if (defeated)
        {
            defeatedTimer -= timeDelta;
            if (defeatedTimer <= 0.0)
            {
                getGameObject()->setActive(false);
            }
            return;
        }
    }

    void MarioPatrolEnemy::handlePlayerContact(MarioPlayer *player, const Collision *collision)
    {
        MarioGame &game = MarioGame::getInstance();
        if (game.isGameWon() || defeated)
        {
            return;
        }

        const bool stomped = collision->isVerticalCollision();

        if (!stomped)
        {
            player->defeat();
            return;
        }

        defeated = true;
        defeatedTimer = squashDuration;

        collider->setCollisionMask(0);

        body->setVelocity(Eigen::Vector2f::Zero());

        animator->play(squashAnimset.get());

        player->bounceAfterStomp();

        audio->replay(*stompSound);
    }

    void MarioPatrolEnemy::onCollisionEnter(const Collision *collision, Collider *other, double timeDelta)
    {
        if (defeated)
        {
            return;
        }

        // if we hit a wall, turn around
        if (collision->isHorizontalCollision())
        {
            direction *= -1.0f;
            Eigen::Vector2f velocity = body->getVelocity();
            velocity.x() = direction * walkSpeed;
            body->setVelocity(velocity);
            sprite->setFlip(direction < 0.0f ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
        }
    }
} // namespace mario