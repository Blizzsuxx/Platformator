#include "doom_projectile.h"

#include "doom_barrel.h"
#include "doom_enemy.h"
#include "doom_player.h"
#include "platformator/behaviorquery.h"
#include "platformator/collider.h"
#include "platformator/runtime.h"

namespace doom
{
    DoomProjectile::DoomProjectile() = default;

    void DoomProjectile::start()
    {
        body = getGameObject()->getComponent<Rigidbody>();
        elapsed = 0.0;
        destroyed = false;
    }

    void DoomProjectile::fixedUpdate(double timeDelta)
    {
        if (destroyed)
        {
            return;
        }
        elapsed += timeDelta;
        if (elapsed >= static_cast<double>(lifetime))
        {
            destroyed = true;
            getGameObject()->destroy();
        }
    }

    void DoomProjectile::onCollisionEnter(const Collision *, Collider *other, double)
    {
        if (destroyed || other == nullptr)
        {
            return;
        }
        GameObject *otherObj = other->getGameObject();
        if (otherObj == nullptr)
        {
            return;
        }

        const uint64_t group = other->getCollisionGroup();
        if ((group & (LAYER_WALL | LAYER_DOOR)) != 0ULL)
        {
            detonate();
            return;
        }

        if (friendly)
        {
            if (auto *enemy = platformator::getBehavior<DoomEnemy>(otherObj))
            {
                enemy->takeDamage(damage);
                detonate();
                return;
            }
            if (auto *barrel = platformator::getBehavior<DoomBarrel>(otherObj))
            {
                barrel->takeDamage(damage);
                detonate();
                return;
            }
        }
        else
        {
            if (auto *player = platformator::getBehavior<DoomPlayer>(otherObj))
            {
                player->takeDamage(damage);
                detonate();
                return;
            }
        }
    }

    void DoomProjectile::launch(const Eigen::Vector2f &direction)
    {
        const float len = direction.norm();
        const Eigen::Vector2f dir = len > 1e-4f ? (direction / len).eval() : Eigen::Vector2f(1.0f, 0.0f);
        if (body == nullptr)
        {
            body = getGameObject()->getComponent<Rigidbody>();
        }
        if (body != nullptr)
        {
            body->setVelocity(dir * speed);
        }
    }

    bool DoomProjectile::isFriendly() const
    {
        return friendly;
    }

    void DoomProjectile::detonate()
    {
        if (destroyed)
        {
            return;
        }
        destroyed = true;
        if (impactFxTemplate)
        {
            platformator::instantiate(impactFxTemplate, getGameObject()->getPosition());
        }
        getGameObject()->destroy();
    }
} // namespace doom
