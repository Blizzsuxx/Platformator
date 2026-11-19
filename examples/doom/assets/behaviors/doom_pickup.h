#pragma once

#include <string>

#include "doom_constants.h"
#include "platformator/assetreference.h"
#include "platformator/behavior.h"
#include "platformator/scriptregistration.h"

namespace doom
{
    class DoomPlayer;

    class DoomPickup : public Behavior
    {
      public:
        DoomPickup();

        void fixedUpdate(double timeDelta) override;
        void onCollisionEnter(const Collision *collision, Collider *other, double timeDelta) override;

        bool collect(DoomPlayer *player);
        int getPickupKind() const;
        bool isCollected() const;

      private:
        int pickupKind = PICKUP_AMMO;
        int amount = 25;
        std::string targetScenePath = "";
        platformator::AudioAssetRef pickupSound;

        bool collected = false;

        SERIALIZABLE_SCRIPT(
            DoomPickup,
            pickupKind,
            amount,
            targetScenePath,
            pickupSound);
    };
} // namespace doom
