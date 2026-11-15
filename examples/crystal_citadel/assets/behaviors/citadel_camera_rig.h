#pragma once

#include "platformator/behavior.h"
#include "platformator/camera.h"
#include "platformator/objectreference.h"
#include "platformator/scriptregistration.h"

namespace citadel
{
    class CitadelCameraRig : public Behavior
    {
      public:
        CitadelCameraRig();

        void lateUpdate(double timeDelta) override;

      private:
        ObjectReference<Camera> camera;
        ObjectReference<GameObject> target;
        float minX = 0.0f;
        float maxX = 2000.0f;
        float minY = -120.0f;
        float maxY = 120.0f;
        float verticalOffset = 30.0f;

        SERIALIZABLE_SCRIPT(
            CitadelCameraRig,
            camera,
            target,
            minX,
            maxX,
            minY,
            maxY,
            verticalOffset);
    };
} // namespace citadel
