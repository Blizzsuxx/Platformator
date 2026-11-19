#include "doom_pickup.h"

#include "doom_game.h"
#include "doom_player.h"
#include "platformator/audio.h"
#include "platformator/behaviorquery.h"
#include "platformator/collider.h"

namespace doom
{
    DoomPickup::DoomPickup() = default;

    void DoomPickup::fixedUpdate(double)
    {
        if (collected)
        {
            return;
        }
        auto *game = DoomGameManager::tryGetInstance();
        auto *player = game ? game->getPlayer() : nullptr;
        if (player == nullptr || player->isDead())
        {
            return;
        }
        const Eigen::Vector2f delta = player->getGameObject()->getPosition() - getGameObject()->getPosition();
        if (delta.squaredNorm() <= 24.0f * 24.0f)
        {
            collect(player);
        }
    }

    void DoomPickup::onCollisionEnter(const Collision *, Collider *other, double)
    {
        if (collected || other == nullptr || other->getGameObject() == nullptr)
        {
            return;
        }
        if (auto *player = platformator::getBehavior<DoomPlayer>(other->getGameObject()))
        {
            collect(player);
        }
    }

    bool DoomPickup::collect(DoomPlayer *player)
    {
        if (collected || player == nullptr || player->isDead())
        {
            return false;
        }

        auto *game = DoomGameManager::tryGetInstance();
        bool consumed = false;

        switch (pickupKind)
        {
        case PICKUP_MEDKIT:
            consumed = player->addHealth(amount);
            if (consumed && game)
            {
                game->triggerScreenFlash(80, 240, 120, 75, 0.2f);
            }
            break;
        case PICKUP_ARMOR:
            consumed = player->addArmor(amount);
            if (consumed && game)
            {
                game->triggerScreenFlash(90, 180, 255, 75, 0.2f);
            }
            break;
        case PICKUP_AMMO:
            consumed = player->addAmmo(amount);
            if (consumed && game)
            {
                game->triggerScreenFlash(240, 210, 80, 65, 0.16f);
            }
            break;
        case PICKUP_SHOTGUN:
            player->giveWeapon(WEAPON_SHOTGUN);
            player->addAmmo(amount);
            player->selectWeapon(WEAPON_SHOTGUN);
            consumed = true;
            if (game)
            {
                game->triggerScreenFlash(255, 220, 110, 80, 0.2f);
            }
            break;
        case PICKUP_PLASMA:
            player->giveWeapon(WEAPON_PLASMA);
            player->addAmmo(amount);
            player->selectWeapon(WEAPON_PLASMA);
            consumed = true;
            if (game)
            {
                game->triggerScreenFlash(100, 220, 255, 85, 0.2f);
            }
            break;
        case PICKUP_RED_KEYCARD:
            if (game)
            {
                game->grantRedKey();
                game->triggerScreenFlash(255, 70, 70, 95, 0.28f);
            }
            consumed = true;
            break;
        case PICKUP_EXIT_PORTAL:
            if (game)
            {
                const std::string dest = !targetScenePath.empty() ? targetScenePath : game->getNextScenePath();
                if (!dest.empty())
                {
                    game->triggerLevelTransition(dest);
                    consumed = true;
                }
            }
            break;
        default:
            break;
        }

        if (!consumed)
        {
            return false;
        }

        collected = true;
        if (auto *audio = getGameObject()->getComponent<Audio>(); audio != nullptr && pickupSound)
        {
            audio->playAndForget(pickupSound.get());
        }
        if (pickupKind != PICKUP_EXIT_PORTAL)
        {
            getGameObject()->destroy();
        }
        return true;
    }

    int DoomPickup::getPickupKind() const
    {
        return pickupKind;
    }

    bool DoomPickup::isCollected() const
    {
        return collected;
    }
} // namespace doom
