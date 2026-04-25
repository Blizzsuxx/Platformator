#pragma once

#include <string>

#include "mario_entity.h"

class Camera;

namespace mario
{
    class MarioPlayer;

    class MarioCameraRig : public MarioEntity
    {
    public:
        MarioCameraRig();

        std::string getTypeName() const override;
        void deserialize(const ScriptDescriptor &descriptor) override;
        void serialize(ScriptDescriptor &descriptor) const override;
        void lateUpdate(double timeDelta) override;

    private:
        Camera *camera;
        MarioPlayer *player;
        std::string target;
        float cameraLead;
        float levelWidth;

        void awake() override;
    };
} // namespace mario