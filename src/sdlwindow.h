#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_mixer.h>
#include <SDL2/SDL_ttf.h>
#include <functional>
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

    void addSdlListener(std::function<void(SDL_Event)> listener);

private:
    SDL_Window *window;
    SDL_Renderer *renderer;
    Camera *mainCamera;
    SDL_Event sdlEvent;
    DebugDraw &debugDraw;
    bool quit;

    std::list<Sprite *> spriteComponents;
    std::vector<std::function<void(SDL_Event)>> listeners;

    bool init();
    void close();
};
