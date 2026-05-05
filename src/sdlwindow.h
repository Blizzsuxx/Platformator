#pragma once

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <functional>
#include <vector>

#include "camera.h"
#include "debugsettings.h"
#include "debugdraw.h"
#include "windowsettings.h"

class AudioWrapper;

class SDLWindow
{
public:
    SDLWindow(const WindowSettings &windowSettings, const DebugSettings &debugSettings);
    ~SDLWindow();

    void handleSDLEvents(double deltaTime);
    void render();
    bool isRunning() const;

    void setMainCamera(Camera *mainCamera);

    SDL_Window *getWindow() const;
    SDL_Renderer *getRenderer() const;
    MIX_Mixer *getMixer() const;
    int getRenderWidth() const;
    int getRenderHeight() const;

    void addSpriteComponent(Sprite *spriteComponent);
    void removeSpriteComponent(Sprite *spriteComponent);

    void addSdlListener(const std::function<void(SDL_Event, double)> &listener);
    bool shouldSimulateFrame() const;
    void clearAdvanceFrameRequest();
    bool getIsFrameAdvanceMode() const;
    void clearDebugObjects();
    Camera *getMainCamera() const;

    bool playAndForget(AudioWrapper *audioWrapper, float gain = 1.0f, int loopCount = 0);
    void updateTransientAudio();
    void clearTransientAudio();

private:
    struct TransientAudioPlayback
    {
        MIX_Track *track;
        AudioWrapper *audioWrapper;
    };

    SDL_Window *window;
    SDL_Renderer *renderer;
    MIX_Mixer *mixer;
    Camera *mainCamera;
    SDL_Event sdlEvent;
    DebugDraw &debugDraw;
    const char *rendererName;
    WindowSettings windowSettings;
    int renderWidth;
    int renderHeight;
    bool quit;
    bool frameAdvanceMode;
    bool advanceFrameRequested;

    std::vector<Sprite *> spriteComponents;
    std::vector<std::function<void(SDL_Event, double)>> listeners;
    std::vector<TransientAudioPlayback> transientAudioPlaybacks;

    bool init();
    void close();
    void updateRenderSize();
    void releaseTransientAudioPlayback(size_t index);
};
