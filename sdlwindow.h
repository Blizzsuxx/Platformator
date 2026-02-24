#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_mixer.h>
#include <SDL2/SDL_ttf.h>
#include "camera.h"

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

private:
    SDL_Window *window;
    SDL_Renderer *renderer;
    Camera *mainCamera;
    SDL_Event sdlEvent;
    bool quit;

    std::list<Sprite *> spriteComponents;

    bool init();
    void close();
};
