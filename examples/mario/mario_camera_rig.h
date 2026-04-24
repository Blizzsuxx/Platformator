#pragma once

class Camera;

namespace mario
{
    class MarioPlayer;

    class MarioCameraRig
    {
    public:
        MarioCameraRig(Camera *camera, MarioPlayer *player);

        void update() const;

    private:
        Camera *camera;
        MarioPlayer *player;
    };
} // namespace mario