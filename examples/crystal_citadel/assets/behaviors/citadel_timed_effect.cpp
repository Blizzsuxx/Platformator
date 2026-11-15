#include "citadel_timed_effect.h"

#include <algorithm>

namespace citadel
{
    void CitadelTimedEffect::start()
    {
        animator = getGameObject()->getComponent<Animator>();
        remainingTime = std::max(0.05f, duration);
        destroying = false;
        if (animator != nullptr && clip.get() != nullptr)
        {
            animator->play(clip.get());
        }
    }

    void CitadelTimedEffect::update(double timeDelta)
    {
        if (destroying)
        {
            return;
        }
        GameObject *self = getGameObject();
        self->setRotation(self->getRotation() + rotationSpeed * static_cast<float>(timeDelta));
        if (riseSpeed != 0.0f)
        {
            self->setPosition(self->getPosition() + Eigen::Vector2f(0.0f, -riseSpeed * static_cast<float>(timeDelta)));
        }

        remainingTime -= timeDelta;
        if (remainingTime <= 0.0)
        {
            destroying = true;
            self->destroy();
        }
    }
} // namespace citadel
