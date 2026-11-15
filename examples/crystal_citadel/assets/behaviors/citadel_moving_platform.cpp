#include "citadel_moving_platform.h"

#include <algorithm>

namespace citadel
{
    void CitadelMovingPlatform::start()
    {
        body = getGameObject()->getComponent<Rigidbody>();
        if (pointA.isApprox(pointB, 1e-3f))
        {
            pointA = getGameObject()->getPosition();
            pointB = pointA + Eigen::Vector2f(120.0f, 0.0f);
        }
    }

    void CitadelMovingPlatform::fixedUpdate(double timeDelta)
    {
        if (body == nullptr || timeDelta <= 0.0)
        {
            return;
        }

        const Eigen::Vector2f pos = getGameObject()->getPosition();
        const Eigen::Vector2f target = movingToB ? pointB : pointA;
        const Eigen::Vector2f delta = target - pos;
        const float dist = delta.norm();

        if (dist <= std::max(2.0f, speed * static_cast<float>(timeDelta)))
        {
            movingToB = !movingToB;
            const Eigen::Vector2f nextTarget = movingToB ? pointB : pointA;
            const Eigen::Vector2f nextDelta = nextTarget - pos;
            const float nextLen = nextDelta.norm();
            body->setVelocity(nextLen > 1e-3f ? Eigen::Vector2f((nextDelta / nextLen) * speed)
                                              : Eigen::Vector2f(0.0f, 0.0f));
            return;
        }

        body->setVelocity((delta / dist) * speed);
    }
} // namespace citadel
