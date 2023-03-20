#pragma once

#include <SDL2/SDL.h>

class SDLWindow
{
public:
    SDLWindow();
    ~SDLWindow();

    bool init();
    void close();

    SDL_Window* getWindow() const;
    SDL_Surface* getSurface() const;

    const int SCREEN_WIDTH = 640;
    const int SCREEN_HEIGHT = 480;

private:
    SDL_Window* window;
    SDL_Surface* screenSurface;
};
