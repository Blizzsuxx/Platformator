#include "citadel_crate.h"

namespace citadel
{
    void CitadelBreakableCrate::start()
    {
        audio = getGameObject()->getComponent<Audio>();
    }

    void CitadelBreakableCrate::takeDamage(int amount)
    {
        if (broken || amount <= 0)
        {
            return;
        }
        hitPoints -= amount;
        if (hitPoints <= 0)
        {
            shatter();
        }
    }

    bool CitadelBreakableCrate::isBroken() const
    {
        return broken;
    }

    void CitadelBreakableCrate::shatter()
    {
        if (broken)
        {
            return;
        }
        broken = true;
        const Eigen::Vector2f pos = getGameObject()->getPosition();

        if (audio != nullptr && breakSound.get() != nullptr)
        {
            audio->playAndForget(breakSound.get());
        }
        if (bool(breakFxTemplate) || breakFxTemplate.hasFilePath())
        {
            platformator::instantiate(breakFxTemplate, pos);
        }
        if (bool(lootTemplate) || lootTemplate.hasFilePath())
        {
            for (int i = 0; i < dropCount; ++i)
            {
                const float offsetX = (i - (dropCount - 1) * 0.5f) * 24.0f;
                platformator::instantiate(lootTemplate, pos + Eigen::Vector2f(offsetX, -6.0f));
            }
        }
        if (bool(bonusLootTemplate) || bonusLootTemplate.hasFilePath())
        {
            platformator::instantiate(bonusLootTemplate, pos + Eigen::Vector2f(0.0f, -22.0f));
        }

        getGameObject()->destroy();
    }
} // namespace citadel
