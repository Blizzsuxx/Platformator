#include "doom_enemy.h"

#include <algorithm>
#include <cmath>

#include "doom_game.h"
#include "doom_player.h"
#include "doom_projectile.h"
#include "platformator/behaviorquery.h"
#include "platformator/runtime.h"

namespace doom
{
    DoomEnemy::DoomEnemy() = default;

    DoomEnemy::~DoomEnemy()
    {
        if (registered)
        {
            if (auto *game = DoomGameManager::tryGetInstance())
            {
                game->unregisterEnemy(this);
            }
            registered = false;
        }
    }

    void DoomEnemy::start()
    {
        body = getGameObject()->getComponent<Rigidbody>();
        animator = getGameObject()->getComponent<Animator>();
        sprite = getGameObject()->getComponent<Sprite>();
        audio = getGameObject()->getComponent<Audio>();

        if (maxHp <= 0)
        {
            maxHp = std::max(1, hp);
        }
        if (hp <= 0)
        {
            hp = maxHp;
        }
        if (body != nullptr)
        {
            body->setGravity(false);
            body->setAngularVelocity(0.0f);
        }
        ensureRegistered();
        if (animator && walkAnim)
        {
            animator->play(walkAnim.get());
        }
    }

    void DoomEnemy::onDestroy()
    {
        if (registered)
        {
            if (auto *game = DoomGameManager::tryGetInstance())
            {
                game->unregisterEnemy(this);
            }
            registered = false;
        }
    }

    void DoomEnemy::ensureRegistered()
    {
        if (registered || defeated)
        {
            return;
        }
        if (auto *game = DoomGameManager::tryGetInstance())
        {
            game->registerEnemy(this);
            registered = true;
        }
    }

    void DoomEnemy::fixedUpdate(double timeDelta)
    {
        if (defeated)
        {
            return;
        }
        ensureRegistered();

        const float dt = static_cast<float>(timeDelta);
        if (attackTimer > 0.0f)
        {
            attackTimer = std::max(0.0f, attackTimer - dt);
        }
        if (hurtTimer > 0.0f)
        {
            hurtTimer = std::max(0.0f, hurtTimer - dt);
            if (hurtTimer <= 0.0f && sprite != nullptr)
            {
                sprite->setColor(255, 255, 255, 255);
            }
        }
        if (animHoldTimer > 0.0f)
        {
            animHoldTimer = std::max(0.0f, animHoldTimer - dt);
        }

        strafeTimer += dt;
        if (strafeTimer >= 1.6f)
        {
            strafeTimer = 0.0f;
            strafeSign = -strafeSign;
        }

        if (body != nullptr)
        {
            body->setAngularVelocity(0.0f);
        }
        getGameObject()->setRotation(0.0f);

        auto *game = DoomGameManager::tryGetInstance();
        auto *player = game ? game->getPlayer() : nullptr;
        if (player == nullptr || player->isDead())
        {
            if (body != nullptr)
            {
                body->setVelocity(Eigen::Vector2f::Zero());
            }
            return;
        }

        const Eigen::Vector2f myPos = getGameObject()->getPosition();
        const Eigen::Vector2f playerPos = player->getGameObject()->getPosition();
        const Eigen::Vector2f toPlayer = playerPos - myPos;
        const float dist = toPlayer.norm();
        const Eigen::Vector2f dir = dist > 1e-3f ? (toPlayer / dist).eval() : Eigen::Vector2f(1.0f, 0.0f);

        bool hasLos = false;
        if (dist <= detectRange)
        {
            if (dist <= 8.0f)
            {
                hasLos = true;
            }
            else
            {
                const auto hit = getRuntime().raycast(myPos, dir, dist + 8.0f, LOS_RAY_MASK, false);
                hasLos = hit.has_value() && hit->gameObject == player->getGameObject();
            }
        }

        if (!alerted && hasLos)
        {
            alerted = true;
            if (audio && alertSound)
            {
                audio->playAndForget(alertSound.get());
            }
        }

        if (!alerted)
        {
            if (body != nullptr)
            {
                body->setVelocity(Eigen::Vector2f::Zero());
            }
            return;
        }

        const float desiredRange = enemyKind == ENEMY_TROOPER ? 140.0f : 120.0f;
        Eigen::Vector2f moveVel = Eigen::Vector2f::Zero();
        if (hurtTimer <= 0.0f)
        {
            const Eigen::Vector2f perp(-dir.y(), dir.x());
            if (!hasLos || dist > desiredRange)
            {
                moveVel = dir * moveSpeed + perp * (moveSpeed * 0.25f * strafeSign);
            }
            else
            {
                moveVel = perp * (moveSpeed * 0.55f * strafeSign);
            }
        }
        if (body != nullptr)
        {
            body->setVelocity(moveVel);
        }

        if (hasLos && dist <= attackRange && attackTimer <= 0.0f && hurtTimer <= 0.0f)
        {
            performAttack(dir);
            attackTimer = attackCooldown;
        }
    }

    void DoomEnemy::update(double)
    {
        if (!registered)
        {
            ensureRegistered();
        }
        updateAnimationState();
    }

    void DoomEnemy::performAttack(const Eigen::Vector2f &dirToPlayer)
    {
        if (animator && attackAnim)
        {
            animator->play(attackAnim.get());
            animHoldTimer = 0.28f;
        }
        if (audio && attackSound)
        {
            audio->playAndForget(attackSound.get());
        }

        auto *game = DoomGameManager::tryGetInstance();
        auto *player = game ? game->getPlayer() : nullptr;
        if (player == nullptr || player->isDead())
        {
            return;
        }

        const Eigen::Vector2f origin = getGameObject()->getPosition();
        auto &runtime = getRuntime();

        if (enemyKind == ENEMY_TROOPER)
        {
            // Line-of-sight hitscan shot blocked cleanly by walls & doors
            const auto hit = runtime.raycast(origin, dirToPlayer, attackRange + 16.0f, LOS_RAY_MASK, false);
            if (hit.has_value() && hit->gameObject == player->getGameObject())
            {
                player->takeDamage(attackDamage);
            }
            return;
        }

        if (!fireballTemplate)
        {
            return;
        }

        const auto spawnBolt = [&](float angleOffset) {
            const float c = std::cos(angleOffset);
            const float s = std::sin(angleOffset);
            const Eigen::Vector2f shotDir(dirToPlayer.x() * c - dirToPlayer.y() * s,
                                          dirToPlayer.x() * s + dirToPlayer.y() * c);
            const Eigen::Vector2f spawnPos = origin + shotDir * 24.0f;
            if (GameObject *projObj = platformator::instantiate(fireballTemplate, spawnPos))
            {
                if (auto *proj = platformator::getBehavior<DoomProjectile>(projObj))
                {
                    proj->launch(shotDir);
                }
            }
        };

        if (enemyKind == ENEMY_BARON)
        {
            spawnBolt(-0.18f);
            spawnBolt(0.0f);
            spawnBolt(0.18f);
        }
        else
        {
            spawnBolt(0.0f);
        }
    }

    void DoomEnemy::updateAnimationState()
    {
        if (animator == nullptr || animHoldTimer > 0.0f)
        {
            return;
        }
        if (walkAnim && animator->getCurrentClip() != walkAnim.get())
        {
            animator->play(walkAnim.get());
        }
    }

    void DoomEnemy::takeDamage(int damage)
    {
        if (defeated || damage <= 0)
        {
            return;
        }
        alerted = true;
        hp = std::max(0, hp - damage);
        hurtTimer = 0.16f;
        if (sprite != nullptr)
        {
            sprite->setColor(255, 85, 85, 255);
        }
        if (animator && hurtAnim)
        {
            animator->play(hurtAnim.get());
            animHoldTimer = 0.18f;
        }

        if (hp > 0)
        {
            if (audio && hurtSound)
            {
                audio->playAndForget(hurtSound.get());
            }
            return;
        }

        defeated = true;
        auto *game = DoomGameManager::tryGetInstance();
        if (registered && game)
        {
            game->unregisterEnemy(this);
            registered = false;
        }
        if (game)
        {
            game->onEnemyDefeated();
        }
        if (audio && deathSound)
        {
            audio->playAndForget(deathSound.get());
        }

        const Eigen::Vector2f pos = getGameObject()->getPosition();
        if (deathFxTemplate)
        {
            platformator::instantiate(deathFxTemplate, pos);
        }
        if (dropTemplate)
        {
            platformator::instantiate(dropTemplate, pos);
        }

        getGameObject()->destroy();
    }

    int DoomEnemy::getHp() const
    {
        return hp;
    }

    int DoomEnemy::getMaxHp() const
    {
        return maxHp;
    }

    bool DoomEnemy::isDefeated() const
    {
        return defeated;
    }

    bool DoomEnemy::isAlerted() const
    {
        return alerted;
    }
} // namespace doom
