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
    MarioPatrolEnemy::MarioPatrolEnemy(GameObject *gameObject, float minX, float maxX, float direction)
        : MarioEntity(gameObject),
          body(gameObject != nullptr ? gameObject->getComponent<Rigidbody>() : nullptr),
          collider(gameObject != nullptr ? static_cast<Collider *>(gameObject->getComponent(ComponentType::COLLIDER)) : nullptr),
          animator(gameObject != nullptr ? gameObject->getComponent<Animator>() : nullptr),
          sprite(gameObject != nullptr ? gameObject->getComponent<Sprite>() : nullptr),
          minX(minX),
          maxX(maxX),
          direction(direction),
          defeated(false),
          defeatedTimer(0.0)
    {
    }

    void MarioPatrolEnemy::update(double timeDelta)
    {
        if (!isActive() || body == nullptr)
        {
            return;
        }

        if (defeated)
        {
            defeatedTimer -= timeDelta;
            if (defeatedTimer <= 0.0)
            {
                gameObject->setActive(false);
            }
            return;
        }

        if (gameObject->getX() <= minX)
        {
            direction = 1.0f;
        }
        else if (gameObject->getX() >= maxX)
        {
            direction = -1.0f;
        }

        Eigen::Vector2f velocity = body->getVelocity();
        velocity.x() = direction * ENEMY_WALK_SPEED;
        body->setVelocity(velocity);

        if (sprite != nullptr)
        {
            sprite->setFlip(direction < 0.0f ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
        }

        playClipIfChanged(animator, "walk");
    }

    void MarioPatrolEnemy::handlePlayerContact(MarioGame &game, MarioPlayer &player)
    {
        if (game.isGameWon() || !isActive() || defeated)
        {
            return;
        }

        const Bounds playerBounds = getBounds(player.getGameObject());
        const Bounds enemyBounds = getBounds(gameObject);
        const float previousPlayerBottom = player.getPositionBeforePhysics().y() + playerBounds.halfExtents.y();
        const float enemyTop = enemyBounds.center.y() - enemyBounds.halfExtents.y();
        const bool stomped = player.getVelocityBeforePhysics().y() > 10.0f && previousPlayerBottom <= enemyTop + STOMP_TOLERANCE;

        if (!stomped)
        {
            player.defeat(game);
            return;
        }

        defeated = true;
        defeatedTimer = ENEMY_SQUASH_DURATION;

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
            gameObject->setActive(false);
            defeatedTimer = 0.0;
        }

        player.bounceAfterStomp();
        game.playStompSound();
    }
} // namespace mario