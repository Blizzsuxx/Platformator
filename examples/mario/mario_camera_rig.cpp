#include "mario_camera_rig.h"

#include <algorithm>

#include "camera.h"
#include "gamemanager.h"
#include "mario_constants.h"
#include "mario_helpers.h"
#include "mario_player.h"
#include "rigidbody.h"

namespace mario
{
    MarioCameraRig::MarioCameraRig()
        : camera(nullptr), player(nullptr), target(), cameraLead(CAMERA_LEAD), levelWidth(LEVEL_WIDTH)
    {
    }

    void MarioCameraRig::start()
    {
        camera = getGameObject()->getComponent<Camera>();
        player = nullptr;
    }

    void MarioCameraRig::lateUpdate(double)
    {
        if (player == nullptr)
        {
            GameObject *targetObject = target.get();
            if (targetObject == nullptr)
            {
                targetObject = target.resolve();
            }

            player = getBehavior<MarioPlayer>(targetObject);
            if (player == nullptr)
            {
                return;
            }
        }

        if (!player->isActive())
        {
            return;
        }

        SDL_FRect cameraRect = camera->getCamera();
        float targetX = player->getGameObject()->getX() - SCREEN_WIDTH * 0.5f;

        Rigidbody *playerBody = player->getBody();
        if (playerBody != nullptr)
        {
            targetX += std::clamp(playerBody->getVelocity().x() * 0.25f, -cameraLead, cameraLead);
        }

        cameraRect.x = std::clamp(targetX, 0.0f, std::max(0.0f, levelWidth - cameraRect.w));
        cameraRect.y = 0.0f;
        camera->setCamera(cameraRect);
    }
} // namespace mario