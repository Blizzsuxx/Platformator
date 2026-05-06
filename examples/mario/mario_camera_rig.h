#pragma once

#include "platformator/behavior.h"
#include "platformator/camera.h"
#include "platformator/objectreference.h"
#include "platformator/rigidbody.h"
#include "platformator/scriptregistration.h"

class Camera;

namespace mario
{
    class MarioPlayer;

    class MarioCameraRig : public Behavior
    {
    public:
        MarioCameraRig();

        void lateUpdate(double timeDelta) override;
        void start() override;

    private:
        ObjectReference<Camera> camera;
        ObjectReference<GameObject> target;
        Rigidbody *targetBody;
        float cameraLead;

        SERIALIZABLE_SCRIPT(
            MarioCameraRig,
            camera,
            target,
            cameraLead);
    };

} // namespace mario