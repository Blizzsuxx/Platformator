#include "mario_camera_rig.h"

#include <algorithm>

#include "platformator/camera.h"

#include "mario_constants.h"
#include "mario_player.h"

namespace mario
{
    MarioCameraRig::MarioCameraRig()
        : camera(), target(), cameraLead()
    {
    }

    void MarioCameraRig::start()
    {
        targetBody = target.get()->getComponent<Rigidbody>();
    }

    void MarioCameraRig::lateUpdate(double)
    {
        SDL_FRect cameraRect = camera.get()->getCamera();
        float targetX = target.get()->getX() - cameraRect.w * 0.5f;

        cameraRect.x = std::max(0.0f, targetX);
        camera.get()->setCamera(cameraRect);
    }
} // namespace mario