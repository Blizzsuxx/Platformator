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
    void applyPhysics();
    void resolveCollisions();
    void render();

    void loop();

    SDL_Window *getWindow() const;
    SDL_Renderer *getRenderer() const;

    const int SCREEN_WIDTH = 640;
    const int SCREEN_HEIGHT = 480;

private:
    SDL_Window *window;
    SDL_Renderer *renderer;
    GameObjectManager *gameObjectManager;
    Camera *mainCamera;
    SDL_Event e;
    bool quit;
};
