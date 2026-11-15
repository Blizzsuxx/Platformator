#include "citadel_orbit_shield.h"

#include <cmath>

namespace citadel
{
    void CitadelOrbitShield::start()
    {
        getGameObject()->setLocalPosition(
            Eigen::Vector2f(std::cos(currentAngle) * orbitRadius, std::sin(currentAngle) * orbitRadius));
    }

    void CitadelOrbitShield::update(double timeDelta)
    {
        if (consumed)
        {
            return;
        }
        currentAngle += orbitSpeed * static_cast<float>(timeDelta);
        getGameObject()->setLocalPosition(
            Eigen::Vector2f(std::cos(currentAngle) * orbitRadius, std::sin(currentAngle) * orbitRadius));
        getGameObject()->setRotation(currentAngle);
    }

    void CitadelOrbitShield::absorbHit()
    {
        if (consumed)
        {
            return;
        }
        consumed = true;
        if (bool(shatterFxTemplate) || shatterFxTemplate.hasFilePath())
        {
            platformator::instantiate(shatterFxTemplate, getGameObject()->getPosition());
        }
        getGameObject()->destroy();
    }

    bool CitadelOrbitShield::isConsumed() const
    {
        return consumed;
    }
} // namespace citadel
