#include "doom_door.h"

#include <algorithm>
#include <cmath>

#include "doom_game.h"
#include "doom_player.h"

namespace doom
{
    DoomDoor::DoomDoor() = default;

    void DoomDoor::start()
    {
        audio = getGameObject()->getComponent<Audio>();
        closedPosition = getGameObject()->getPosition();
        Eigen::Vector2f dir(slideDirX, slideDirY);
        const float len = dir.norm();
        slideDir = len > 1e-4f ? (dir / len).eval() : Eigen::Vector2f(0.0f, -1.0f);
        openProgress = 0.0f;
        targetOpen = false;
        openHoldTimer = 0.0f;
        lockedCooldown = 0.0f;
    }

    void DoomDoor::fixedUpdate(double timeDelta)
    {
        const float dt = static_cast<float>(timeDelta);
        if (lockedCooldown > 0.0f)
        {
            lockedCooldown = std::max(0.0f, lockedCooldown - dt);
        }

        auto *game = DoomGameManager::tryGetInstance();
        auto *player = game ? game->getPlayer() : nullptr;
        bool playerNear = false;
        if (player && !player->isDead())
        {
            const Eigen::Vector2f diff = player->getGameObject()->getPosition() - closedPosition;
            const float dist = diff.norm();
            playerNear = dist < 56.0f;
            if (dist < 46.0f && !targetOpen)
            {
                const bool hasKey = game->hasRedKey();
                if (!requiresRedKey || hasKey)
                {
                    tryActivate(hasKey);
                }
            }
        }

        if (targetOpen)
        {
            if (openHoldTimer > 0.0f && !playerNear)
            {
                openHoldTimer = std::max(0.0f, openHoldTimer - dt);
                if (openHoldTimer <= 0.0f && autoCloseDelay > 0.0f)
                {
                    targetOpen = false;
                    if (audio && doorSound)
                    {
                        audio->playAndForget(doorSound.get());
                    }
                }
            }
            else if (playerNear && autoCloseDelay > 0.0f)
            {
                openHoldTimer = autoCloseDelay;
            }
        }

        const float prevProgress = openProgress;
        const float rate = slideDistance > 1.0f ? (slideSpeed / slideDistance) : 2.0f;
        if (targetOpen && openProgress < 1.0f)
        {
            openProgress = std::min(1.0f, openProgress + rate * dt);
        }
        else if (!targetOpen && openProgress > 0.0f && !playerNear)
        {
            openProgress = std::max(0.0f, openProgress - rate * dt);
        }

        if (openProgress != prevProgress)
        {
            getGameObject()->setPosition(closedPosition + slideDir * (openProgress * slideDistance));
        }
    }

    bool DoomDoor::tryActivate(bool hasRedKey)
    {
        if (isLockedFor(hasRedKey))
        {
            if (lockedCooldown <= 0.0f)
            {
                lockedCooldown = 0.4f;
                if (audio && lockedSound)
                {
                    audio->playAndForget(lockedSound.get());
                }
                if (auto *game = DoomGameManager::tryGetInstance())
                {
                    game->triggerScreenFlash(220, 40, 40, 90, 0.2f);
                }
            }
            return false;
        }

        if (!targetOpen)
        {
            targetOpen = true;
            openHoldTimer = autoCloseDelay;
            if (audio && doorSound)
            {
                audio->playAndForget(doorSound.get());
            }
        }
        else
        {
            openHoldTimer = autoCloseDelay;
        }
        return true;
    }

    bool DoomDoor::isLockedFor(bool hasRedKey) const
    {
        return requiresRedKey && !hasRedKey;
    }

    bool DoomDoor::isOpen() const
    {
        return openProgress > 0.75f;
    }

    bool DoomDoor::requiresKey() const
    {
        return requiresRedKey;
    }

    float DoomDoor::getOpenProgress() const
    {
        return openProgress;
    }
} // namespace doom
