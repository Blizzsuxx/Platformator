#include "doom_timed_fx.h"

namespace doom
{
    DoomTimedFx::DoomTimedFx() = default;

    void DoomTimedFx::start()
    {
        elapsed = 0.0;
    }

    void DoomTimedFx::update(double timeDelta)
    {
        elapsed += timeDelta;
        if (elapsed >= static_cast<double>(lifetime))
        {
            getGameObject()->destroy();
        }
    }
} // namespace doom
