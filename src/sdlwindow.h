#pragma once

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <functional>
#include <vector>

#include "platformator/camera.h"
#include "platformator/debugsettings.h"
#include "debugdraw.h"
#include "renderculling.h"
#include "platformator/windowsettings.h"

class Audio;
class AudioWrapper;
class Sprite;

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
    bool isHeadless() const;

    void addSpriteComponent(Sprite *spriteComponent);
    void removeSpriteComponent(Sprite *spriteComponent);
    void notifySpriteGeometryChanged(Sprite *spriteComponent) noexcept;
    void releaseRenderCullingStorage() noexcept;

    void addSdlListener(const std::function<void(SDL_Event, double)> &listener);
    bool shouldSimulateFrame() const;
    bool shouldUseFixedStepDelta() const;
    void clearAdvanceFrameRequest();
    void clearDebugObjects();
    Camera *getMainCamera() const;

    bool playAndForget(AudioWrapper *audioWrapper, float gain = 1.0f, int loopCount = 0);
    void updateAudio();
    void clearTransientAudio();
    void scheduleAudioReplay(Audio *audio);
    void cancelAudioReplay(Audio *audio);

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
    bool paused;
    bool advanceFrameRequested;
    platformator_detail::RenderCullingMode requestedRenderCullingMode;
    platformator_detail::RenderCullingMode effectiveRenderCullingMode;
    std::unique_ptr<platformator_detail::RenderCullingState> renderCullingState;

    std::vector<Sprite *> spriteComponents;
    std::vector<std::function<void(SDL_Event, double)>> listeners;
    std::vector<TransientAudioPlayback> transientAudioPlaybacks;
    std::vector<Audio *> pendingAudioReplays;

    bool init();
    void close();
    void updateRenderSize();
    bool prepareCachedLinear(Camera *camera, const platformator_detail::SpriteRenderView &view);
    void releaseTransientAudioPlayback(size_t index);
};
