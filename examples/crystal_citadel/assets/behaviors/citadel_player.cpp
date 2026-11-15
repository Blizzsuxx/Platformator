#include "citadel_player.h"

#include <algorithm>
#include <cmath>

#include "platformator/behaviorquery.h"
#include "platformator/collision.h"
#include "platformator/runtime.h"

#include "citadel_boss.h"
#include "citadel_crate.h"
#include "citadel_game.h"
#include "citadel_orbit_shield.h"
#include "citadel_patrol_enemy.h"
#include "citadel_projectile.h"
#include "citadel_splitting_slime.h"

namespace citadel
{
    CitadelPlayer::CitadelPlayer() = default;

    void CitadelPlayer::start()
    {
        body = getGameObject()->getComponent<Rigidbody>();
        animator = getGameObject()->getComponent<Animator>();
        sprite = getGameObject()->getComponent<Sprite>();
        audio = getGameObject()->getComponent<Audio>();
        spawnPosition = getGameObject()->getPosition();
        if (hp <= 0)
        {
            hp = maxHp;
        }
        if (auto *game = CitadelGameManager::tryGetInstance())
        {
            game->onPlayerStatusChanged();
        }
    }

    void CitadelPlayer::fixedUpdate(double timeDelta)
    {
        if (fireTimer > 0.0)
        {
            fireTimer = std::max(0.0, fireTimer - timeDelta);
        }
        if (invulnerableTimer > 0.0)
        {
            invulnerableTimer = std::max(0.0, invulnerableTimer - timeDelta);
        }
        updateInput(timeDelta);
    }

    void CitadelPlayer::update(double)
    {
        updateAnimation();
    }

    void CitadelPlayer::onCollisionEnter(const Collision *collision, Collider *other, double)
    {
        if (other == nullptr)
        {
            return;
        }
        GameObject *otherObj = other->getGameObject();
        if (otherObj == nullptr)
        {
            return;
        }

        if (other->getIsTrigger())
        {
            if (otherObj->getTag() == "death")
            {
                takeDamage(1);
                respawn();
                return;
            }
            if (otherObj->getTag() == "hazard")
            {
                takeDamage(1);
                bounce(0.75f);
            }
            return;
        }

        const bool fromAbove = collision != nullptr &&
                               collision->isVerticalCollision() &&
                               getGameObject()->getY() < otherObj->getY() - 4.0f &&
                               (body == nullptr || body->getVelocity().y() >= -25.0f);

        if (auto *enemy = getBehavior<CitadelPatrolEnemy>(otherObj))
        {
            if (enemy->isDefeated())
            {
                return;
            }
            if (fromAbove)
            {
                enemy->takeDamage(1);
                bounce(0.75f);
            }
            else
            {
                takeDamage(1);
            }
            return;
        }

        if (auto *slime = getBehavior<CitadelSplittingSlime>(otherObj))
        {
            if (slime->isDefeated())
            {
                return;
            }
            if (fromAbove)
            {
                slime->takeDamage(1);
                bounce(0.75f);
            }
            else
            {
                takeDamage(1);
            }
            return;
        }

        if (auto *crate = getBehavior<CitadelBreakableCrate>(otherObj))
        {
            if (fromAbove && !crate->isBroken())
            {
                crate->takeDamage(1);
                bounce(0.6f);
            }
            return;
        }

        if (auto *boss = getBehavior<CitadelArchonBoss>(otherObj))
        {
            if (boss->isDefeated())
            {
                return;
            }
            if (fromAbove)
            {
                boss->takeDamage(1);
                bounce(0.9f);
            }
            else
            {
                takeDamage(1);
            }
        }
    }

    bool CitadelPlayer::fireBolt()
    {
        if (fireTimer > 0.0)
        {
            return false;
        }
        if (!bool(boltTemplate) && !boltTemplate.hasFilePath())
        {
            return false;
        }

        const Eigen::Vector2f pos = getGameObject()->getPosition() + Eigen::Vector2f(facing * 24.0f, -2.0f);
        GameObject *bolt = platformator::instantiate(boltTemplate, pos);
        if (bolt == nullptr)
        {
            return false;
        }
        if (auto *proj = getBehavior<CitadelProjectile>(bolt))
        {
            proj->configureVelocity(facing * BOLT_SPEED, 0.0f, true, 1);
        }
        if (audio != nullptr && shootSound.get() != nullptr)
        {
            audio->playAndForget(shootSound.get());
        }
        fireTimer = fireCooldown;
        ++boltsFired;
        return true;
    }

    bool CitadelPlayer::grantOrbitShield()
    {
        if (hasActiveShield())
        {
            return false;
        }
        if (!bool(shieldTemplate) && !shieldTemplate.hasFilePath())
        {
            return false;
        }
        GameObject *shield = platformator::instantiate(shieldTemplate, getGameObject());
        if (auto *game = CitadelGameManager::tryGetInstance())
        {
            game->onPlayerStatusChanged();
        }
        return shield != nullptr;
    }

    void CitadelPlayer::heal(int amount)
    {
        if (amount <= 0)
        {
            return;
        }
        hp = std::min(maxHp, hp + amount);
        if (auto *game = CitadelGameManager::tryGetInstance())
        {
            game->onPlayerStatusChanged();
        }
    }

    void CitadelPlayer::takeDamage(int amount)
    {
        if (amount <= 0 || invulnerableTimer > 0.0)
        {
            return;
        }

        for (GameObject *child : getGameObject()->getChildren())
        {
            if (auto *shield = getBehavior<CitadelOrbitShield>(child))
            {
                if (!shield->isConsumed())
                {
                    shield->absorbHit();
                    invulnerableTimer = 0.65;
                    if (auto *game = CitadelGameManager::tryGetInstance())
                    {
                        game->onPlayerStatusChanged();
                    }
                    return;
                }
            }
        }

        hp = std::max(0, hp - amount);
        invulnerableTimer = 0.75;
        if (audio != nullptr && hurtSound.get() != nullptr)
        {
            audio->playAndForget(hurtSound.get());
        }
        if (hp <= 0)
        {
            hp = maxHp;
            respawn();
        }
        if (auto *game = CitadelGameManager::tryGetInstance())
        {
            game->onPlayerStatusChanged();
        }
    }

    void CitadelPlayer::bounce(float factor)
    {
        if (body == nullptr)
        {
            return;
        }
        Eigen::Vector2f velocity = body->getVelocity();
        velocity.y() = -jumpSpeed * factor;
        body->setVelocity(velocity);
        airJumpsRemaining = 1;
    }

    void CitadelPlayer::respawn()
    {
        getGameObject()->setPosition(spawnPosition);
        if (body != nullptr)
        {
            body->setVelocity(Eigen::Vector2f::Zero());
        }
        jumpWasPressed = false;
        airJumpsRemaining = 1;
    }

    int CitadelPlayer::getHp() const
    {
        return hp;
    }

    int CitadelPlayer::getMaxHp() const
    {
        return maxHp;
    }

    int CitadelPlayer::getBoltsFired() const
    {
        return boltsFired;
    }

    float CitadelPlayer::getFacing() const
    {
        return facing;
    }

    bool CitadelPlayer::hasActiveShield() const
    {
        if (getGameObject() == nullptr)
        {
            return false;
        }
        for (GameObject *child : getGameObject()->getChildren())
        {
            if (auto *shield = getBehavior<CitadelOrbitShield>(child))
            {
                if (!shield->isConsumed())
                {
                    return true;
                }
            }
        }
        return false;
    }

    Rigidbody *CitadelPlayer::getBody() const
    {
        return body;
    }

    void CitadelPlayer::updateInput(double timeDelta)
    {
        if (body == nullptr)
        {
            return;
        }
        auto &runtime = getRuntime();
        const bool respawnPressed = runtime.isKeyDown(SDL_SCANCODE_R);
        if (respawnPressed && !respawnWasPressed)
        {
            respawn();
        }
        respawnWasPressed = respawnPressed;

        const bool moveLeft = runtime.isKeyDown(SDL_SCANCODE_LEFT) || runtime.isKeyDown(SDL_SCANCODE_A);
        const bool moveRight = runtime.isKeyDown(SDL_SCANCODE_RIGHT) || runtime.isKeyDown(SDL_SCANCODE_D);
        const bool jumpPressed = runtime.isKeyDown(SDL_SCANCODE_SPACE) || runtime.isKeyDown(SDL_SCANCODE_UP) ||
                                 runtime.isKeyDown(SDL_SCANCODE_W);
        const SDL_MouseButtonFlags mouseButtons = runtime.getMouseState(nullptr, nullptr);
        const bool firePressed = runtime.isKeyDown(SDL_SCANCODE_F) || runtime.isKeyDown(SDL_SCANCODE_J) ||
                                 (mouseButtons & SDL_BUTTON_LMASK) != 0;

        float horizontalVelocity = 0.0f;
        if (moveLeft != moveRight)
        {
            facing = moveLeft ? -1.0f : 1.0f;
            horizontalVelocity = facing * walkSpeed;
        }

        const bool grounded = body->hasSupportContact();
        if (grounded)
        {
            airJumpsRemaining = 1;
        }

        Eigen::Vector2f velocity = body->getVelocity();
        velocity.x() = horizontalVelocity;
        velocity.y() = std::min(velocity.y() + gravity * static_cast<float>(timeDelta), maxFallSpeed);

        if (sprite != nullptr)
        {
            sprite->setFlip(facing < 0.0f ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
        }

        if (jumpPressed && !jumpWasPressed)
        {
            if (grounded)
            {
                velocity.y() = -jumpSpeed;
                if (audio != nullptr && jumpSound.get() != nullptr)
                {
                    audio->replay(jumpSound.get());
                }
            }
            else if (airJumpsRemaining > 0)
            {
                --airJumpsRemaining;
                velocity.y() = -jumpSpeed * 0.92f;
                spawnJumpBurst();
                if (audio != nullptr && jumpSound.get() != nullptr)
                {
                    audio->replay(jumpSound.get());
                }
            }
        }

        body->setVelocity(velocity);
        jumpWasPressed = jumpPressed;

        if (firePressed && !fireWasPressed)
        {
            fireBolt();
        }
        fireWasPressed = firePressed;
    }

    void CitadelPlayer::updateAnimation()
    {
        if (animator == nullptr || body == nullptr)
        {
            return;
        }
        const Eigen::Vector2f velocity = body->getVelocity();
        if (!body->hasSupportContact())
        {
            if (velocity.y() < -5.0f && jumpAnim.get() != nullptr)
            {
                animator->play(jumpAnim.get());
            }
            else if (fallAnim.get() != nullptr)
            {
                animator->play(fallAnim.get());
            }
            return;
        }

        if (std::abs(velocity.x()) > 5.0f)
        {
            if (runAnim.get() != nullptr)
            {
                animator->play(runAnim.get());
            }
        }
        else if (idleAnim.get() != nullptr)
        {
            animator->play(idleAnim.get());
        }
    }

    void CitadelPlayer::spawnJumpBurst()
    {
        if (!bool(jumpFxTemplate) && !jumpFxTemplate.hasFilePath())
        {
            return;
        }
        platformator::instantiate(jumpFxTemplate, getGameObject()->getPosition() + Eigen::Vector2f(0.0f, 14.0f));
    }
} // namespace citadel
