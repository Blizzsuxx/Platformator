#pragma once

#include <SDL3/SDL_pixels.h>

struct WindowSettings
{
    int width = 640;
    int height = 480;
    bool fullscreen = false;
    bool maximizeOnStartup = false;
    bool keepAspectRatio = false;
    // Strict physics-only mode (never initializes SDL video/audio). For full-scene tests, use hidden = true with SDL dummy drivers.
    bool headless = false;
    bool hidden = false;
    bool enableRenderCulling = true;
    bool highPixelDensity = false;
    SDL_Color clearColor{255, 255, 255, 255};
};
