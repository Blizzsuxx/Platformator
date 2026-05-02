#include "mario_camera_rig.h"

#include <algorithm>

#include "camera.h"
#include "gamemanager.h"
#include "mario_constants.h"
#include "mario_player.h"
#include "rigidbody.h"

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
        float targetX = target.get()->getX() - SCREEN_WIDTH * 0.5f;

        cameraRect.x = std::max(0.0f, targetX);
        camera.get()->setCamera(cameraRect);
    }
} // namespace mario