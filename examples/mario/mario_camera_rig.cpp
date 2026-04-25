#include "mario_camera_rig.h"

#include <algorithm>

#include "behaviorfactoryregistry.h"
#include "camera.h"
#include "gamemanager.h"
#include "mario_constants.h"
#include "mario_helpers.h"
#include "mario_player.h"
#include "rigidbody.h"

namespace mario
{
    MarioCameraRig::MarioCameraRig() : camera(nullptr), player(nullptr), target("Player"), cameraLead(CAMERA_LEAD), levelWidth(LEVEL_WIDTH)
    {
    }

    std::string MarioCameraRig::getTypeName() const
    {
        return "MarioCameraRig";
    }

    void MarioCameraRig::deserialize(const ScriptDescriptor &descriptor)
    {
        target = descriptor.getString("target", target);
        cameraLead = descriptor.getFloat("cameralead", cameraLead);
        levelWidth = descriptor.getFloat("levelwidth", levelWidth);
    }

    void MarioCameraRig::serialize(ScriptDescriptor &descriptor) const
    {
        descriptor.setStringProperty("target", target);
        descriptor.setFloatProperty("cameralead", cameraLead);
        descriptor.setFloatProperty("levelwidth", levelWidth);
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
            player = getBehavior<MarioPlayer>(GameManager::getInstance().getGameObject(target));
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

    REGISTER_BEHAVIOR(MarioCameraRig);
} // namespace mario