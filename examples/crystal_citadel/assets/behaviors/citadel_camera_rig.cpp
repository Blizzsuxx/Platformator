#include "citadel_camera_rig.h"

#include <algorithm>

namespace citadel
{
    CitadelCameraRig::CitadelCameraRig() = default;

    void CitadelCameraRig::lateUpdate(double)
    {
        Camera *cam = camera.get();
        GameObject *tgt = target.get();
        if (cam == nullptr || tgt == nullptr)
        {
            return;
        }

        SDL_FRect cameraRect = cam->getCamera();
        const float desiredX = tgt->getX() - cameraRect.w * 0.5f;
        const float desiredY = tgt->getY() - cameraRect.h * 0.5f - verticalOffset;

        cameraRect.x = std::clamp(desiredX, minX, std::max(minX, maxX));
        cameraRect.y = std::clamp(desiredY, minY, std::max(minY, maxY));
        cam->setCamera(cameraRect);
    }
} // namespace citadel
