#pragma once

#include "gamemanager.h"
#include "jsonhelpers.h"
#include "behavior.h"
#include "assetreference.h"
#include "objectreference.h"
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