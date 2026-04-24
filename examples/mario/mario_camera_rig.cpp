#include "mario_camera_rig.h"

#include <algorithm>

#include "camera.h"
#include "mario_constants.h"
#include "mario_player.h"
#include "rigidbody.h"

namespace mario
{
    MarioCameraRig::MarioCameraRig(Camera *camera, MarioPlayer *player) : camera(camera), player(player)
    {
    }

    void MarioCameraRig::update() const
    {
        if (camera == nullptr || player == nullptr || !player->isActive())
        {
            return;
        }

        SDL_FRect cameraRect = camera->getCamera();
        float targetX = player->getGameObject()->getX() - SCREEN_WIDTH * 0.5f;

        Rigidbody *playerBody = player->getBody();
        if (playerBody != nullptr)
        {
            targetX += std::clamp(playerBody->getVelocity().x() * 0.25f, -CAMERA_LEAD, CAMERA_LEAD);
        }

        cameraRect.x = std::clamp(targetX, 0.0f, std::max(0.0f, LEVEL_WIDTH - cameraRect.w));
        cameraRect.y = 0.0f;
        camera->setCamera(cameraRect);
    }
} // namespace mario