#pragma once

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <functional>
#include <vector>
#include "camera.h"
#include "debugdraw.h"

class SDLWindow
{
public:
    SDLWindow();
    ~SDLWindow();

    void handleEvents();
    void render();
    bool isRunning() const;

    void setMainCamera(Camera *mainCamera);

    SDL_Window *getWindow() const;
    SDL_Renderer *getRenderer() const;

    void addSpriteComponent(Sprite *spriteComponent);
    void removeSpriteComponent(Sprite *spriteComponent);

    void addSdlListener(const std::function<void(SDL_Event)> &listener);
    bool shouldSimulateFrame() const;
    void clearAdvanceFrameRequest();
    bool getIsFrameAdvanceMode() const;
    void clearDebugObjects();

private:
    SDL_Window *window;
    SDL_Renderer *renderer;
    Camera *mainCamera;
    SDL_Event sdlEvent;
    DebugDraw &debugDraw;
    const char *rendererName;
    bool quit;
    bool frameAdvanceMode;
    bool advanceFrameRequested;

    std::vector<Sprite *> spriteComponents;
    std::vector<std::function<void(SDL_Event)>> listeners;

    bool init();
    void close();
};
