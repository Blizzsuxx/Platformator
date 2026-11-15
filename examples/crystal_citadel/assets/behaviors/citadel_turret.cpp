#include "citadel_turret.h"

#include <cmath>

#include "platformator/behaviorquery.h"

#include "citadel_game.h"
#include "citadel_player.h"
#include "citadel_projectile.h"

namespace citadel
{
    void CitadelArcaneTurret::start()
    {
        audio = getGameObject()->getComponent<Audio>();
        fireTimer = fireInterval * 0.65;
    }

    void CitadelArcaneTurret::update(double timeDelta)
    {
        const Eigen::Vector2f dir = computeAimDirection();
        if (GameObject *barrel = barrelObject.get())
        {
            const float angle = std::atan2(dir.y(), dir.x());
            barrel->setRotation(angle);
        }
        if (Sprite *bSprite = barrelSprite.get())
        {
            bSprite->setFlip(dir.x() < 0.0f ? SDL_FLIP_VERTICAL : SDL_FLIP_NONE);
        }

        fireTimer -= timeDelta;
        if (fireTimer <= 0.0)
        {
            fireTimer = fireInterval;
            fireShotNow();
        }
    }

    bool CitadelArcaneTurret::fireShotNow()
    {
        if (!bool(boltTemplate) && !boltTemplate.hasFilePath())
        {
            return false;
        }

        const Eigen::Vector2f dir = computeAimDirection();
        if (auto *game = CitadelGameManager::tryGetInstance())
        {
            if (auto *player = game->getPlayer())
            {
                const float dist = (player->getGameObject()->getPosition() - getGameObject()->getPosition()).norm();
                if (dist > detectRange)
                {
                    return false;
                }
            }
        }

        const Eigen::Vector2f basePos = barrelObject.get() != nullptr ? barrelObject->getPosition()
                                                                      : getGameObject()->getPosition();
        const Eigen::Vector2f muzzlePos = basePos + dir * 20.0f;

        GameObject *bolt = platformator::instantiate(boltTemplate, muzzlePos);
        if (bolt == nullptr)
        {
            return false;
        }
        if (auto *proj = getBehavior<CitadelProjectile>(bolt))
        {
            proj->configureVelocity(dir.x() * boltSpeed, dir.y() * boltSpeed, false, 1);
        }
        if (bool(muzzleFxTemplate) || muzzleFxTemplate.hasFilePath())
        {
            platformator::instantiate(muzzleFxTemplate, muzzlePos);
        }
        if (audio != nullptr && fireSound.get() != nullptr)
        {
            audio->playAndForget(fireSound.get());
        }
        ++shotsFired;
        return true;
    }

    bool CitadelArcaneTurret::hasResolvedChildren() const
    {
        return barrelObject.get() != nullptr && barrelSprite.get() != nullptr &&
               barrelObject->getParent() == getGameObject();
    }

    GameObject *CitadelArcaneTurret::getBarrelObject() const
    {
        return barrelObject.get();
    }

    Sprite *CitadelArcaneTurret::getBarrelSprite() const
    {
        return barrelSprite.get();
    }

    int CitadelArcaneTurret::getShotsFired() const
    {
        return shotsFired;
    }

    Eigen::Vector2f CitadelArcaneTurret::computeAimDirection() const
    {
        const Eigen::Vector2f origin = barrelObject.get() != nullptr ? barrelObject->getPosition()
                                                                     : getGameObject()->getPosition();
        if (auto *game = CitadelGameManager::tryGetInstance())
        {
            if (auto *player = game->getPlayer())
            {
                const Eigen::Vector2f delta = player->getGameObject()->getPosition() - origin;
                const float len = delta.norm();
                if (len > 1e-3f)
                {
                    return delta / len;
                }
            }
        }
        return Eigen::Vector2f(-1.0f, 0.0f);
    }
} // namespace citadel
