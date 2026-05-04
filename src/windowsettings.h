#pragma once

#include "constants.h"

struct WindowSettings
{
    int width = static_cast<int>(SCREEN_WIDTH);
    int height = static_cast<int>(SCREEN_HEIGHT);
    bool fullscreen = false;
    bool maximizeOnStartup = false;
    bool keepAspectRatio = false;
};