#include "citadel_collectible.h"

#include "platformator/behaviorquery.h"
#include "platformator/collider.h"

#include "citadel_game.h"
#include "citadel_player.h"

namespace citadel
{
    void CitadelCollectible::start()
    {
        animator = getGameObject()->getComponent<Animator>();
        audio = getGameObject()->getComponent<Audio>();
        if (animator != nullptr && idleAnim.get() != nullptr)
        {
            animator->play(idleAnim.get());
        }
        if (gemValue > 0)
        {
            if (auto *game = CitadelGameManager::tryGetInstance())
            {
                game->registerSceneGem(gemValue);
            }
        }
    }

    void CitadelCollectible::onCollisionEnter(const Collision *, Collider *other, double)
    {
        if (collected || other == nullptr)
        {
            return;
        }
        if (auto *player = getBehavior<CitadelPlayer>(other->getGameObject()))
        {
            collect(player);
        }
    }

    bool CitadelCollectible::collect(CitadelPlayer *player)
    {
        if (collected || player == nullptr)
        {
            return false;
        }
        collected = true;

        if (audio != nullptr && pickupSound.get() != nullptr)
        {
            audio->playAndForget(pickupSound.get());
        }
        if (bool(pickupFxTemplate) || pickupFxTemplate.hasFilePath())
        {
            platformator::instantiate(pickupFxTemplate, getGameObject()->getPosition());
        }
        if (gemValue > 0)
        {
            if (auto *game = CitadelGameManager::tryGetInstance())
            {
                game->onGemCollected(gemValue);
            }
        }
        if (healAmount > 0)
        {
            player->heal(healAmount);
        }
        if (grantsShield)
        {
            player->grantOrbitShield();
        }

        getGameObject()->destroy();
        return true;
    }

    bool CitadelCollectible::isCollected() const
    {
        return collected;
    }

    int CitadelCollectible::getGemValue() const
    {
        return gemValue;
    }
} // namespace citadel
