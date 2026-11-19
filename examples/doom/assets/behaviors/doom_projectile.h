#pragma once

#include <Eigen/Dense>

#include "doom_constants.h"
#include "platformator/assetreference.h"
#include "platformator/behavior.h"
#include "platformator/rigidbody.h"
#include "platformator/scriptregistration.h"

namespace doom
{
    class DoomProjectile : public Behavior
    {
      public:
        DoomProjectile();

        void start() override;
        void fixedUpdate(double timeDelta) override;
        void onCollisionEnter(const Collision *collision, Collider *other, double timeDelta) override;

        void launch(const Eigen::Vector2f &direction);
        bool isFriendly() const;

      private:
        bool friendly = true;
        float speed = 440.0f;
        int damage = 20;
        float lifetime = 2.0f;
        platformator::TemplateAssetRef impactFxTemplate;

        Rigidbody *body = nullptr;
        double elapsed = 0.0;
        bool destroyed = false;

        void detonate();

        SERIALIZABLE_SCRIPT(
            DoomProjectile,
            friendly,
            speed,
            damage,
            lifetime,
            impactFxTemplate);
    };
} // namespace doom
