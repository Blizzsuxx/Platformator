#include "doom_barrel.h"

#include <algorithm>
#include <vector>

#include "doom_enemy.h"
#include "doom_game.h"
#include "doom_player.h"
#include "platformator/audio.h"
#include "platformator/runtime.h"

namespace doom
{
    DoomBarrel::DoomBarrel() = default;

    DoomBarrel::~DoomBarrel()
    {
        if (registered)
        {
            if (auto *game = DoomGameManager::tryGetInstance())
            {
                game->unregisterBarrel(this);
            }
            registered = false;
        }
    }

    void DoomBarrel::start()
    {
        sprite = getGameObject()->getComponent<Sprite>();
        ensureRegistered();
    }

    void DoomBarrel::update(double timeDelta)
    {
        if (!registered)
        {
            ensureRegistered();
        }
        if (flashTimer > 0.0f)
        {
            flashTimer = std::max(0.0f, flashTimer - static_cast<float>(timeDelta));
            if (sprite != nullptr && flashTimer <= 0.0f)
            {
                sprite->setColor(255, 255, 255, 255);
            }
        }
    }

    void DoomBarrel::onDestroy()
    {
        if (registered)
        {
            if (auto *game = DoomGameManager::tryGetInstance())
            {
                game->unregisterBarrel(this);
            }
            registered = false;
        }
    }

    void DoomBarrel::ensureRegistered()
    {
        if (registered || exploded)
        {
            return;
        }
        if (auto *game = DoomGameManager::tryGetInstance())
        {
            game->registerBarrel(this);
            registered = true;
        }
    }

    void DoomBarrel::takeDamage(int damage)
    {
        if (exploded || damage <= 0)
        {
            return;
        }
        hp -= damage;
        flashTimer = 0.09f;
        if (sprite != nullptr)
        {
            sprite->setColor(255, 160, 100, 255);
        }
        if (hp <= 0)
        {
            explode();
        }
    }

    int DoomBarrel::getHp() const
    {
        return hp;
    }

    bool DoomBarrel::isExploded() const
    {
        return exploded;
    }

    void DoomBarrel::explode()
    {
        if (exploded)
        {
            return;
        }
        exploded = true;

        auto *game = DoomGameManager::tryGetInstance();
        if (registered && game)
        {
            game->unregisterBarrel(this);
            registered = false;
        }

        const Eigen::Vector2f origin = getGameObject()->getPosition();
        auto &runtime = getRuntime();

        if (explosionTemplate)
        {
            platformator::instantiate(explosionTemplate, origin);
        }
        if (auto *audio = getGameObject()->getComponent<Audio>(); audio != nullptr && explodeSound)
        {
            audio->playAndForget(explodeSound.get());
        }

        const auto hasBlastLos = [&](const Eigen::Vector2f &targetPos) -> bool {
            const Eigen::Vector2f delta = targetPos - origin;
            const float dist = delta.norm();
            if (dist <= 4.0f)
            {
                return true;
            }
            const auto hit = runtime.raycast(origin, delta / dist, dist - 2.0f, WALL_RAY_MASK, false);
            return !hit.has_value();
        };

        if (game)
        {
            game->triggerScreenFlash(255, 140, 40, 95, 0.22f);

            const std::vector<DoomEnemy *> enemySnapshot = game->getEnemies();
            for (DoomEnemy *enemy : enemySnapshot)
            {
                if (enemy == nullptr || enemy->isDefeated())
                {
                    continue;
                }
                const Eigen::Vector2f pos = enemy->getGameObject()->getPosition();
                const float dist = (pos - origin).norm();
                if (dist <= blastRadius && hasBlastLos(pos))
                {
                    const float falloff = 1.0f - 0.65f * (dist / blastRadius);
                    enemy->takeDamage(std::max(1, static_cast<int>(static_cast<float>(blastDamage) * falloff)));
                }
            }

            const std::vector<DoomBarrel *> barrelSnapshot = game->getBarrels();
            for (DoomBarrel *other : barrelSnapshot)
            {
                if (other == nullptr || other == this || other->isExploded())
                {
                    continue;
                }
                const Eigen::Vector2f pos = other->getGameObject()->getPosition();
                const float dist = (pos - origin).norm();
                if (dist <= blastRadius && hasBlastLos(pos))
                {
                    const float falloff = 1.0f - 0.65f * (dist / blastRadius);
                    other->takeDamage(std::max(1, static_cast<int>(static_cast<float>(blastDamage) * falloff)));
                }
            }

            if (DoomPlayer *player = game->getPlayer(); player != nullptr && !player->isDead())
            {
                const Eigen::Vector2f pos = player->getGameObject()->getPosition();
                const float dist = (pos - origin).norm();
                if (dist <= blastRadius && hasBlastLos(pos))
                {
                    const float falloff = 1.0f - 0.7f * (dist / blastRadius);
                    player->takeDamage(std::max(1, static_cast<int>(static_cast<float>(blastDamage) * 0.55f * falloff)));
                }
            }
        }

        getGameObject()->destroy();
    }
} // namespace doom
