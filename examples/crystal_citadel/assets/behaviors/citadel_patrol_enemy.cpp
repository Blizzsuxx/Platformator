#include "citadel_patrol_enemy.h"

#include "platformator/collision.h"

#include "citadel_game.h"

namespace citadel
{
    void CitadelPatrolEnemy::start()
    {
        body = getGameObject()->getComponent<Rigidbody>();
        animator = getGameObject()->getComponent<Animator>();
        sprite = getGameObject()->getComponent<Sprite>();
        audio = getGameObject()->getComponent<Audio>();

        if (minX >= maxX)
        {
            const float x = getGameObject()->getX();
            minX = x - 90.0f;
            maxX = x + 90.0f;
        }
        applyDirection();
        if (animator != nullptr && walkAnim.get() != nullptr)
        {
            animator->play(walkAnim.get());
        }
    }

    void CitadelPatrolEnemy::fixedUpdate(double timeDelta)
    {
        if (defeated)
        {
            defeatedTimer -= timeDelta;
            if (defeatedTimer <= 0.0)
            {
                getGameObject()->destroy();
            }
            return;
        }

        if (body != nullptr)
        {
            const float x = getGameObject()->getX();
            if (x <= minX && direction < 0.0f)
            {
                direction = 1.0f;
                applyDirection();
            }
            else if (x >= maxX && direction > 0.0f)
            {
                direction = -1.0f;
                applyDirection();
            }
            else
            {
                Eigen::Vector2f vel = body->getVelocity();
                vel.x() = direction * walkSpeed;
                vel.y() = std::min(vel.y() + PLAYER_GRAVITY * static_cast<float>(timeDelta), PLAYER_MAX_FALL_SPEED);
                body->setVelocity(vel);
            }
        }
    }

    void CitadelPatrolEnemy::onCollisionEnter(const Collision *collision, Collider *other, double)
    {
        if (defeated || other == nullptr || other->getIsTrigger())
        {
            return;
        }
        if (collision != nullptr && collision->isHorizontalCollision())
        {
            direction *= -1.0f;
            applyDirection();
        }
    }

    void CitadelPatrolEnemy::configurePatrol(float newMinX, float newMaxX, float newDirection)
    {
        minX = newMinX;
        maxX = newMaxX;
        direction = newDirection >= 0.0f ? 1.0f : -1.0f;
        if (body != nullptr)
        {
            applyDirection();
        }
    }

    void CitadelPatrolEnemy::takeDamage(int amount)
    {
        if (defeated || amount <= 0)
        {
            return;
        }
        hp -= amount;
        if (audio != nullptr && hitSound.get() != nullptr)
        {
            audio->playAndForget(hitSound.get());
        }
        if (hp > 0)
        {
            return;
        }

        defeated = true;
        defeatedTimer = ENEMY_SQUASH_DURATION;
        if (body != nullptr)
        {
            body->setVelocity(Eigen::Vector2f::Zero());
        }
        if (animator != nullptr && squashAnim.get() != nullptr)
        {
            animator->play(squashAnim.get());
        }

        const Eigen::Vector2f pos = getGameObject()->getPosition();
        if (bool(deathFxTemplate) || deathFxTemplate.hasFilePath())
        {
            platformator::instantiate(deathFxTemplate, pos);
        }
        if (bool(dropTemplate) || dropTemplate.hasFilePath())
        {
            platformator::instantiate(dropTemplate, pos + Eigen::Vector2f(0.0f, -6.0f));
        }
        if (auto *game = CitadelGameManager::tryGetInstance())
        {
            game->onEnemyDefeated();
        }
    }

    bool CitadelPatrolEnemy::isDefeated() const
    {
        return defeated;
    }

    void CitadelPatrolEnemy::applyDirection()
    {
        if (body != nullptr)
        {
            Eigen::Vector2f vel = body->getVelocity();
            vel.x() = direction * walkSpeed;
            body->setVelocity(vel);
        }
        if (sprite != nullptr)
        {
            sprite->setFlip(direction < 0.0f ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
        }
    }
} // namespace citadel
