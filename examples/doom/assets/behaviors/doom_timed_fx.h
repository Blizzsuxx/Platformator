#pragma once

#include "platformator/behavior.h"
#include "platformator/scriptregistration.h"

namespace doom
{
    class DoomTimedFx : public Behavior
    {
      public:
        DoomTimedFx();

        void start() override;
        void update(double timeDelta) override;

      private:
        float lifetime = 0.25f;
        double elapsed = 0.0;

        SERIALIZABLE_SCRIPT(DoomTimedFx, lifetime);
    };
} // namespace doom
