#pragma once

#include <Eigen/Dense>

#include "doom_constants.h"
#include "platformator/assetreference.h"
#include "platformator/audio.h"
#include "platformator/behavior.h"
#include "platformator/scriptregistration.h"

namespace doom
{
    class DoomDoor : public Behavior
    {
      public:
        DoomDoor();

        void start() override;
        void fixedUpdate(double timeDelta) override;

        bool tryActivate(bool hasRedKey);
        bool isLockedFor(bool hasRedKey) const;
        bool isOpen() const;
        bool requiresKey() const;
        float getOpenProgress() const;

      private:
        float slideDirX = 0.0f;
        float slideDirY = -1.0f;
        float slideDistance = 60.0f;
        float slideSpeed = 140.0f;
        bool requiresRedKey = false;
        float autoCloseDelay = 4.5f;
        platformator::AudioAssetRef doorSound;
        platformator::AudioAssetRef lockedSound;

        Audio *audio = nullptr;
        Eigen::Vector2f closedPosition = Eigen::Vector2f::Zero();
        Eigen::Vector2f slideDir = Eigen::Vector2f(0.0f, -1.0f);
        float openProgress = 0.0f;
        bool targetOpen = false;
        float openHoldTimer = 0.0f;
        float lockedCooldown = 0.0f;

        SERIALIZABLE_SCRIPT(
            DoomDoor,
            slideDirX,
            slideDirY,
            slideDistance,
            slideSpeed,
            requiresRedKey,
            autoCloseDelay,
            doorSound,
            lockedSound);
    };
} // namespace doom
