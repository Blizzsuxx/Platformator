// Load/free a real PNG repeatedly without linking SDL_mixer.
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <cstdio>

int main(int argc, char **argv)
{
    if (argc != 2 || !SDL_Init(0))
        return 2;
    std::fprintf(stderr, "Runtime SDL=%d SDL_image=%d\n", SDL_GetVersion(), IMG_Version());
    for (int i = 0; i < 10; ++i)
    {
        SDL_Surface *surface = IMG_Load(argv[1]);
        if (!surface)
        {
            std::fprintf(stderr, "%s\n", SDL_GetError());
            SDL_Quit();
            return 3;
        }
        SDL_DestroySurface(surface);
    }
    SDL_Quit();
    return 0;
}
