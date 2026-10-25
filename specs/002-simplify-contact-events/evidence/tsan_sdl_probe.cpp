#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <cstdio>
int main()
{
    for (int cycle = 0; cycle < 2; ++cycle)
    {
        if (!SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO) || !MIX_Init() || !TTF_Init())
            return 2;
        auto *mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
        auto *window = SDL_CreateWindow("probe", 640, 480, SDL_WINDOW_RESIZABLE);
        auto *renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
        if (!mixer || !window || !renderer)
        {
            std::fprintf(stderr, "%s\n", SDL_GetError());
            return 3;
        }
        MIX_DestroyMixer(mixer);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        MIX_Quit();
        TTF_Quit();
        SDL_Quit();
    }
}
