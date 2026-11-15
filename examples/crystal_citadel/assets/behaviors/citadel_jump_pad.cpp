#include "citadel_jump_pad.h"

#include "platformator/behaviorquery.h"
#include "platformator/collider.h"

#include "citadel_player.h"

namespace citadel
{
    void CitadelJumpPad::start()
    {
        audio = getGameObject()->getComponent<Audio>();
    }

    void CitadelJumpPad::onCollisionEnter(const Collision *, Collider *other, double)
    {
        if (other == nullptr)
        {
            return;
        }
        if (auto *player = getBehavior<CitadelPlayer>(other->getGameObject()))
        {
            player->bounce(bounceFactor);
            if (audio != nullptr && bounceSound.get() != nullptr)
            {
                audio->playAndForget(bounceSound.get());
            }
            if (bool(burstFxTemplate) || burstFxTemplate.hasFilePath())
            {
                platformator::instantiate(burstFxTemplate, getGameObject()->getPosition() + Eigen::Vector2f(0.0f, -10.0f));
            }
        }
    }
} // namespace citadel
