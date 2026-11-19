#pragma once

#include "platformator/behavior.h"
#include "platformator/scriptregistration.h"
#include "platformator/sprite.h"

namespace doom
{
    class DoomBillboard : public Behavior
    {
      public:
        DoomBillboard();
        ~DoomBillboard() override;

        void start() override;
        void update(double timeDelta) override;
        void onDestroy() override;

        float getWorldHeight() const;
        float getVerticalOffset() const;
        bool isFullBright() const;

        TextureRegion getCurrentRegion() const;
        SDL_Color getTint() const;

      private:
        float worldHeight = 48.0f;
        float verticalOffset = 0.0f;
        bool fullBright = false;

        bool registered = false;

        void ensureRegistered();

        SERIALIZABLE_SCRIPT(
            DoomBillboard,
            worldHeight,
            verticalOffset,
            fullBright);
    };
} // namespace doom
