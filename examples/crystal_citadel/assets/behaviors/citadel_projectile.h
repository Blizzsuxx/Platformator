#pragma once

#include "platformator/assetreference.h"
#include "platformator/behavior.h"
#include "platformator/rigidbody.h"
#include "platformator/scriptregistration.h"
#include "platformator/sprite.h"

namespace citadel
{
    class CitadelProjectile : public Behavior
    {
      public:
        CitadelProjectile() = default;

        void start() override;
        void fixedUpdate(double timeDelta) override;
        void onCollisionEnter(const Collision *collision, Collider *other, double timeDelta) override;

        void configureVelocity(float vx, float vy, bool isFriendly, int dmg = 1);
        bool isFriendly() const;

      private:
        float speedX = 360.0f;
        float speedY = 0.0f;
        float lifetime = 1.6f;
        int damage = 1;
        bool friendly = true;
        platformator::TemplateAssetRef impactFxTemplate;

        Rigidbody *body = nullptr;
        Sprite *sprite = nullptr;
        double remainingLife = 0.0;
        bool consumed = false;

        void detonate();

        SERIALIZABLE_SCRIPT(
            CitadelProjectile,
            speedX,
            speedY,
            lifetime,
            damage,
            friendly,
            impactFxTemplate);
    };
} // namespace citadel
