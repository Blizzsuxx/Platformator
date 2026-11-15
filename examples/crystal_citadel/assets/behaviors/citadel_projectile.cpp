#include "citadel_projectile.h"

#include "platformator/behaviorquery.h"
#include "platformator/collider.h"

#include "citadel_boss.h"
#include "citadel_crate.h"
#include "citadel_orbit_shield.h"
#include "citadel_patrol_enemy.h"
#include "citadel_player.h"
#include "citadel_splitting_slime.h"
#include "citadel_turret.h"

namespace citadel
{
    void CitadelProjectile::start()
    {
        body = getGameObject()->getComponent<Rigidbody>();
        sprite = getGameObject()->getComponent<Sprite>();
        remainingLife = lifetime;
        if (body != nullptr)
        {
            body->setVelocity(Eigen::Vector2f(speedX, speedY));
        }
        if (sprite != nullptr)
        {
            sprite->setFlip(speedX < 0.0f ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
        }
    }

    void CitadelProjectile::fixedUpdate(double timeDelta)
    {
        if (consumed)
        {
            return;
        }
        remainingLife -= timeDelta;
        if (remainingLife <= 0.0)
        {
            consumed = true;
            getGameObject()->destroy();
        }
    }

    void CitadelProjectile::onCollisionEnter(const Collision *, Collider *other, double)
    {
        if (consumed || other == nullptr)
        {
            return;
        }
        GameObject *otherObj = other->getGameObject();
        if (otherObj == nullptr)
        {
            return;
        }

        if (friendly)
        {
            if (auto *enemy = getBehavior<CitadelPatrolEnemy>(otherObj))
            {
                if (!enemy->isDefeated())
                {
                    enemy->takeDamage(damage);
                    detonate();
                }
                return;
            }
            if (auto *slime = getBehavior<CitadelSplittingSlime>(otherObj))
            {
                if (!slime->isDefeated())
                {
                    slime->takeDamage(damage);
                    detonate();
                }
                return;
            }
            if (auto *boss = getBehavior<CitadelArchonBoss>(otherObj))
            {
                if (!boss->isDefeated())
                {
                    boss->takeDamage(damage);
                    detonate();
                }
                return;
            }
            if (auto *crate = getBehavior<CitadelBreakableCrate>(otherObj))
            {
                if (!crate->isBroken())
                {
                    crate->takeDamage(damage);
                    detonate();
                }
                return;
            }
            if (!other->getIsTrigger() && otherObj->getTag() == "solid")
            {
                detonate();
            }
            return;
        }

        // Enemy projectile
        if (auto *shield = getBehavior<CitadelOrbitShield>(otherObj))
        {
            if (!shield->isConsumed())
            {
                shield->absorbHit();
                detonate();
            }
            return;
        }
        if (auto *player = getBehavior<CitadelPlayer>(otherObj))
        {
            player->takeDamage(damage);
            detonate();
            return;
        }
        if (getBehavior<CitadelPatrolEnemy>(otherObj) || getBehavior<CitadelSplittingSlime>(otherObj) ||
            getBehavior<CitadelArcaneTurret>(otherObj) || getBehavior<CitadelArchonBoss>(otherObj))
        {
            return;
        }
        if (!other->getIsTrigger() && otherObj->getTag() == "solid")
        {
            detonate();
        }
    }

    void CitadelProjectile::configureVelocity(float vx, float vy, bool isFriendly, int dmg)
    {
        speedX = vx;
        speedY = vy;
        friendly = isFriendly;
        damage = dmg;
        if (body != nullptr)
        {
            body->setVelocity(Eigen::Vector2f(speedX, speedY));
        }
        if (sprite != nullptr)
        {
            sprite->setFlip(speedX < 0.0f ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
        }
    }

    bool CitadelProjectile::isFriendly() const
    {
        return friendly;
    }

    void CitadelProjectile::detonate()
    {
        if (consumed)
        {
            return;
        }
        consumed = true;
        if (bool(impactFxTemplate) || impactFxTemplate.hasFilePath())
        {
            platformator::instantiate(impactFxTemplate, getGameObject()->getPosition());
        }
        getGameObject()->destroy();
    }
} // namespace citadel
