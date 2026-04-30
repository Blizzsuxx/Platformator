#pragma once

#include "mario_entity.h"
#include "jsonhelpers.h"
#include "objectreference.h"

class Camera;

namespace mario
{
    class MarioPlayer;

    class MarioCameraRig : public MarioEntity
    {
    public:
        MarioCameraRig();

        void start() override;
        void lateUpdate(double timeDelta) override;

    private:
        Camera *camera;
        MarioPlayer *player;
        ObjectReference<GameObject> target;
        float cameraLead;
        float levelWidth;
    };

    REGISTER_SCRIPT(
        MarioCameraRig,
        target,
        cameraLead,
        levelWidth);

} // namespace mario