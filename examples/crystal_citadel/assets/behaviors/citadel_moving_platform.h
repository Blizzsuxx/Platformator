#pragma once

#include <Eigen/Dense>

#include "platformator/behavior.h"
#include "platformator/rigidbody.h"
#include "platformator/scriptregistration.h"

namespace citadel
{
    class CitadelMovingPlatform : public Behavior
    {
      public:
        CitadelMovingPlatform() = default;

        void start() override;
        void fixedUpdate(double timeDelta) override;

      private:
        Eigen::Vector2f pointA = Eigen::Vector2f::Zero();
        Eigen::Vector2f pointB = Eigen::Vector2f(120.0f, 0.0f);
        float speed = 65.0f;
        bool movingToB = true;

        Rigidbody *body = nullptr;

        SERIALIZABLE_SCRIPT(
            CitadelMovingPlatform,
            pointA,
            pointB,
            speed,
            movingToB);
    };
} // namespace citadel
