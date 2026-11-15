#include "citadel_splitting_slime.h"

#include <algorithm>

#include "platformator/behaviorquery.h"
#include "platformator/collision.h"

#include "citadel_game.h"

namespace citadel
{
    void CitadelSplittingSlime::start()
    {
        body = getGameObject()->getComponent<Rigidbody>();
        animator = getGameObject()->getComponent<Animator>();
        sprite = getGameObject()->getComponent<Sprite>();
        audio = getGameObject()->getComponent<Audio>();

        if (minX >= maxX)
        {
            const float x = getGameObject()->getX();
            minX = x - 110.0f;
            maxX = x + 110.0f;
        }
        if (body != nullptr)
        {
            body->setVelocity(Eigen::Vector2f(direction * walkSpeed, -65.0f));
        }
        if (sprite != nullptr)
        {
            sprite->setFlip(direction < 0.0f ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
        }
        if (animator != nullptr && bounceAnim.get() != nullptr)
        {
            animator->play(bounceAnim.get());
        }
    }

    void CitadelSplittingSlime::fixedUpdate(double timeDelta)
    {
        if (defeated || body == nullptr)
        {
            return;
        }

        const float x = getGameObject()->getX();
        if (x <= minX && direction < 0.0f)
        {
            direction = 1.0f;
        }
        else if (x >= maxX && direction > 0.0f)
        {
            direction = -1.0f;
        }

        Eigen::Vector2f vel = body->getVelocity();
        vel.x() = direction * walkSpeed;
        vel.y() = std::min(vel.y() + PLAYER_GRAVITY * static_cast<float>(timeDelta), PLAYER_MAX_FALL_SPEED);

        hopTimer -= timeDelta;
        if (hopTimer <= 0.0 && body->hasSupportContact())
        {
            vel.y() = -145.0f;
            hopTimer = 0.95;
        }

        body->setVelocity(vel);
        if (sprite != nullptr)
        {
            sprite->setFlip(direction < 0.0f ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
        }
    }

    void CitadelSplittingSlime::onCollisionEnter(const Collision *collision, Collider *other, double)
    {
        if (defeated || other == nullptr || other->getIsTrigger())
        {
            return;
        }
        if (collision != nullptr && collision->isHorizontalCollision())
        {
            direction *= -1.0f;
        }
    }

    void CitadelSplittingSlime::configureSpawn(int newGeneration, float newDirection, float newWalkSpeed,
                                               float newMinX, float newMaxX, int newHp)
    {
        generation = newGeneration;
        direction = newDirection >= 0.0f ? 1.0f : -1.0f;
        walkSpeed = newWalkSpeed;
        minX = newMinX;
        maxX = newMaxX;
        hp = newHp;
    }

    void CitadelSplittingSlime::takeDamage(int amount)
    {
        if (defeated || amount <= 0)
        {
            return;
        }
        hp -= amount;
        if (hp <= 0)
        {
            splitAndDefeat();
        }
    }

    int CitadelSplittingSlime::getGeneration() const
    {
        return generation;
    }

    bool CitadelSplittingSlime::isDefeated() const
    {
        return defeated;
    }

    void CitadelSplittingSlime::splitAndDefeat()
    {
        if (defeated)
        {
            return;
        }
        defeated = true;
        const Eigen::Vector2f pos = getGameObject()->getPosition();

        if (audio != nullptr && splitSound.get() != nullptr)
        {
            audio->playAndForget(splitSound.get());
        }
        if (bool(splitFxTemplate) || splitFxTemplate.hasFilePath())
        {
            platformator::instantiate(splitFxTemplate, pos);
        }

        if (generation < maxGeneration && (bool(selfTemplate) || selfTemplate.hasFilePath()))
        {
            const Eigen::Vector2f childScale = getGameObject()->getScale() * 0.72f;
            for (int side : {-1, 1})
            {
                const Eigen::Vector2f offset(side * 18.0f, -8.0f);
                GameObject *child = platformator::instantiate(selfTemplate, pos + offset);
                if (child != nullptr)
                {
                    child->setScale(childScale);
                    if (auto *childSlime = getBehavior<CitadelSplittingSlime>(child))
                    {
                        childSlime->configureSpawn(generation + 1, static_cast<float>(side),
                                                   walkSpeed * 1.25f, minX - 24.0f, maxX + 24.0f, 1);
                    }
                }
            }
        }
        else if (bool(gemTemplate) || gemTemplate.hasFilePath())
        {
            platformator::instantiate(gemTemplate, pos + Eigen::Vector2f(0.0f, -6.0f));
        }

        if (auto *game = CitadelGameManager::tryGetInstance())
        {
            game->onEnemyDefeated();
        }
        getGameObject()->destroy();
    }
} // namespace citadel
