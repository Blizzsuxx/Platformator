#include "citadel_boss.h"

#include <cmath>

#include "platformator/behaviorquery.h"

#include "citadel_game.h"
#include "citadel_player.h"
#include "citadel_portal.h"
#include "citadel_projectile.h"
#include "citadel_splitting_slime.h"

namespace citadel
{
    void CitadelArchonBoss::start()
    {
        body = getGameObject()->getComponent<Rigidbody>();
        audio = getGameObject()->getComponent<Audio>();
        if (hp <= 0)
        {
            hp = maxHp;
        }
        hoverY = getGameObject()->getY();
    }

    void CitadelArchonBoss::fixedUpdate(double timeDelta)
    {
        if (defeated || body == nullptr)
        {
            return;
        }

        phaseTime += timeDelta;
        const float x = getGameObject()->getX();
        if (x <= arenaMinX && direction < 0.0f)
        {
            direction = 1.0f;
        }
        else if (x >= arenaMaxX && direction > 0.0f)
        {
            direction = -1.0f;
        }

        const float targetY = hoverY + std::sin(static_cast<float>(phaseTime * 2.2)) * 26.0f;
        const float vy = (targetY - getGameObject()->getY()) * 3.5f;
        body->setVelocity(Eigen::Vector2f(direction * moveSpeed, vy));
    }

    void CitadelArchonBoss::update(double timeDelta)
    {
        if (defeated)
        {
            return;
        }

        const float t = static_cast<float>(phaseTime);
        if (GameObject *crown = crownChild.get())
        {
            crown->setLocalPosition(Eigen::Vector2f(0.0f, -42.0f + std::sin(t * 4.0f) * 4.0f));
        }
        if (GameObject *leftOrb = leftOrbChild.get())
        {
            leftOrb->setLocalPosition(Eigen::Vector2f(-42.0f, std::cos(t * 3.0f) * 10.0f));
            leftOrb->setRotation(t * 2.5f);
        }
        if (GameObject *rightOrb = rightOrbChild.get())
        {
            rightOrb->setLocalPosition(Eigen::Vector2f(42.0f, -std::cos(t * 3.0f) * 10.0f));
            rightOrb->setRotation(-t * 2.5f);
        }

        attackTimer -= timeDelta;
        if (attackTimer <= 0.0)
        {
            attackTimer = attackInterval;
            fireVolleyNow();
        }

        summonTimer -= timeDelta;
        if (summonTimer <= 0.0)
        {
            summonTimer = summonInterval;
            summonMinionNow();
        }
    }

    void CitadelArchonBoss::takeDamage(int amount)
    {
        if (defeated || amount <= 0)
        {
            return;
        }
        hp -= amount;
        if (hp <= 0)
        {
            hp = 0;
            defeatBoss();
        }
    }

    bool CitadelArchonBoss::fireVolleyNow()
    {
        if (defeated || (!bool(bossBoltTemplate) && !bossBoltTemplate.hasFilePath()))
        {
            return false;
        }

        Eigen::Vector2f targetPos = getGameObject()->getPosition() + Eigen::Vector2f(0.0f, 120.0f);
        if (auto *game = CitadelGameManager::tryGetInstance())
        {
            if (auto *player = game->getPlayer())
            {
                targetPos = player->getGameObject()->getPosition();
            }
        }

        bool fired = false;
        for (GameObject *orb : {leftOrbChild.get(), rightOrbChild.get()})
        {
            const Eigen::Vector2f origin = orb != nullptr ? orb->getPosition() : getGameObject()->getPosition();
            Eigen::Vector2f dir = targetPos - origin;
            const float len = dir.norm();
            dir = len > 1e-3f ? (dir / len).eval() : Eigen::Vector2f(0.0f, 1.0f);

            GameObject *bolt = platformator::instantiate(bossBoltTemplate, origin);
            if (bolt != nullptr)
            {
                if (auto *proj = getBehavior<CitadelProjectile>(bolt))
                {
                    proj->configureVelocity(dir.x() * 210.0f, dir.y() * 210.0f, false, 1);
                }
                fired = true;
            }
        }
        return fired;
    }

    bool CitadelArchonBoss::summonMinionNow()
    {
        if (defeated || (!bool(minionTemplate) && !minionTemplate.hasFilePath()))
        {
            return false;
        }

        const Eigen::Vector2f spawnPos = getGameObject()->getPosition() + Eigen::Vector2f(0.0f, 44.0f);
        if (bool(explosionFxTemplate) || explosionFxTemplate.hasFilePath())
        {
            platformator::instantiate(explosionFxTemplate, spawnPos);
        }

        GameObject *minion = platformator::instantiate(minionTemplate, spawnPos);
        if (minion == nullptr)
        {
            return false;
        }
        if (auto *slime = getBehavior<CitadelSplittingSlime>(minion))
        {
            slime->configureSpawn(2, direction, SLIME_WALK_SPEED * 1.15f, arenaMinX, arenaMaxX, 1);
        }
        return true;
    }

    bool CitadelArchonBoss::hasResolvedChildren() const
    {
        GameObject *self = getGameObject();
        return crownChild.get() != nullptr && leftOrbChild.get() != nullptr && rightOrbChild.get() != nullptr &&
               crownChild->getParent() == self && leftOrbChild->getParent() == self &&
               rightOrbChild->getParent() == self;
    }

    int CitadelArchonBoss::getHp() const
    {
        return hp;
    }

    int CitadelArchonBoss::getMaxHp() const
    {
        return maxHp;
    }

    bool CitadelArchonBoss::isDefeated() const
    {
        return defeated;
    }

    GameObject *CitadelArchonBoss::getCrownChild() const
    {
        return crownChild.get();
    }

    GameObject *CitadelArchonBoss::getLeftOrbChild() const
    {
        return leftOrbChild.get();
    }

    GameObject *CitadelArchonBoss::getRightOrbChild() const
    {
        return rightOrbChild.get();
    }

    void CitadelArchonBoss::defeatBoss()
    {
        if (defeated)
        {
            return;
        }
        defeated = true;
        const Eigen::Vector2f pos = getGameObject()->getPosition();

        if (bool(explosionFxTemplate) || explosionFxTemplate.hasFilePath())
        {
            for (const Eigen::Vector2f &offset :
                 {Eigen::Vector2f(-32.0f, -16.0f), Eigen::Vector2f(0.0f, 0.0f), Eigen::Vector2f(32.0f, 16.0f)})
            {
                platformator::instantiate(explosionFxTemplate, pos + offset);
            }
        }

        if (bool(rewardGemTemplate) || rewardGemTemplate.hasFilePath())
        {
            for (int i = -1; i <= 1; ++i)
            {
                platformator::instantiate(rewardGemTemplate, Eigen::Vector2f(pos.x() + i * 44.0f, 382.0f));
            }
        }

        if (bool(defeatPortalTemplate) || defeatPortalTemplate.hasFilePath())
        {
            const float portalX = (arenaMinX + arenaMaxX) * 0.5f;
            GameObject *portal = platformator::instantiate(defeatPortalTemplate, Eigen::Vector2f(portalX, 376.0f));
            if (portal != nullptr)
            {
                if (auto *portalBeh = getBehavior<CitadelLevelPortal>(portal))
                {
                    portalBeh->setTargetScenePath("assets/scenes/hub.scene");
                }
            }
        }

        if (auto *game = CitadelGameManager::tryGetInstance())
        {
            game->onEnemyDefeated();
        }
        getGameObject()->destroy();
    }
} // namespace citadel
