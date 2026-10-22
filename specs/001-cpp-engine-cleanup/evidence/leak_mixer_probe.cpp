#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <cstdlib>
#include <cstdio>

int main(int argc, char **argv)
{
    const int repetitions = argc > 1 ? std::atoi(argv[1]) : 1;
    for (int i = 0; i < repetitions; ++i)
    {
        if (!SDL_Init(0))
            return 2;
        if (!MIX_Init())
        {
            std::fprintf(stderr, "%s\n", SDL_GetError());
            SDL_Quit();
            return 3;
        }
        MIX_Quit();
        SDL_Quit();
    }
    return 0;
}
