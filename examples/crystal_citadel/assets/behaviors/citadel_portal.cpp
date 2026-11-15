#include "citadel_portal.h"

#include "platformator/behaviorquery.h"
#include "platformator/collider.h"

#include "citadel_game.h"
#include "citadel_player.h"

namespace citadel
{
    void CitadelLevelPortal::start()
    {
        animator = getGameObject()->getComponent<Animator>();
        audio = getGameObject()->getComponent<Audio>();
        if (animator != nullptr && portalAnim.get() != nullptr)
        {
            animator->play(portalAnim.get());
        }
    }

    void CitadelLevelPortal::onCollisionEnter(const Collision *, Collider *other, double)
    {
        if (triggered || other == nullptr)
        {
            return;
        }
        if (getBehavior<CitadelPlayer>(other->getGameObject()) != nullptr)
        {
            activatePortal();
        }
    }

    void CitadelLevelPortal::setTargetScenePath(const std::string &path)
    {
        targetScenePath = path;
    }

    const std::string &CitadelLevelPortal::getTargetScenePath() const
    {
        return targetScenePath;
    }

    bool CitadelLevelPortal::activatePortal()
    {
        if (triggered || targetScenePath.empty())
        {
            return false;
        }
        if (auto *game = CitadelGameManager::tryGetInstance())
        {
            if (game->getCollectedGems() < requiredGems)
            {
                return false;
            }
        }

        triggered = true;
        if (audio != nullptr && warpSound.get() != nullptr)
        {
            audio->playAndForget(warpSound.get());
        }
        if (bool(warpFxTemplate) || warpFxTemplate.hasFilePath())
        {
            platformator::instantiate(warpFxTemplate, getGameObject()->getPosition());
        }

        if (auto *game = CitadelGameManager::tryGetInstance())
        {
            game->triggerLevelTransition(targetScenePath);
        }
        else
        {
            getRuntime().requestLoadScene(targetScenePath);
        }
        return true;
    }
} // namespace citadel
