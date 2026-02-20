#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_mixer.h>
#include <SDL2/SDL_ttf.h>
#include "gameobjectmanager.h"
#include "camera.h"

class SDLWindow
{
public:
    SDLWindow();
    ~SDLWindow();

    bool init();
    void close();

    void handleEvents();
    void render();
    bool isRunning() const;

    SDL_Window *getWindow() const;
    SDL_Renderer *getRenderer() const;

    const int SCREEN_WIDTH = 640;
    const int SCREEN_HEIGHT = 480;

private:
    SDL_Window *window;
    SDL_Renderer *renderer;
    Camera *mainCamera;
    SDL_Event sdlEvent;
    bool quit;
};
