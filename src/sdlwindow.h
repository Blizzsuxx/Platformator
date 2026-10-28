#pragma once

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <cstdint>
#include <functional>

#include <json.hpp>
#include <memory>
#include <vector>

#include "platformator/camera.h"
#include "platformator/debugsettings.h"
#include "debugdraw.h"
#include "renderculling.h"
#include "platformator/windowsettings.h"

class Audio;
class AudioWrapper;
class GameObject;
class Sprite;

class SDLWindow
{
  public:
    SDLWindow(const WindowSettings &windowSettings, const DebugSettings &debugSettings,
              const SpatialSettings &spatialSettings = {}, uint64_t runtimeSerial = 0);
    ~SDLWindow();

    void handleSDLEvents(double deltaTime);
    void render();
    void setImageValidationEnabled(bool enabled);
    nlohmann::json getImageValidationProof() const;
    bool isRunning() const;

    void setMainCamera(Camera *mainCamera);
    void markSpriteDirty(Sprite *sprite);
    void markGameObjectDirty(GameObject *gameObject);
    void markViewDirty();

    SDL_Window *getWindow() const;
    SDL_Renderer *getRenderer() const;
    MIX_Mixer *getMixer() const;
    int getRenderWidth() const;
    int getRenderHeight() const;
    bool isHeadless() const;
    const platformator_detail::RenderCullingFrameStats &getRenderCullingStats() const;
    size_t getRenderCullingOwnedCapacityBytes() const;
    nlohmann::json getRendererSignature() const;

    void addSpriteComponent(Sprite *spriteComponent);
    void removeSpriteComponent(Sprite *spriteComponent);

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
    SpatialSettings spatialSettings;
    uint64_t runtimeSerial;
    int renderWidth;
    int renderHeight;
    bool quit;
    bool paused;
    bool advanceFrameRequested;

    bool imageValidationEnabled = false;
    size_t imageValidationFrames = 0;
    uint64_t imageReferenceHash = 1469598103934665603ull;
    uint64_t imageResultHash = 1469598103934665603ull;
    void validateSubmitAllImage();
    std::vector<Sprite *> spriteComponents;
    std::unique_ptr<platformator_detail::RenderCulling> renderCulling;
    platformator_detail::RenderCullingFrameStats renderCullingStats;
    uint64_t geometryRevision;
    uint64_t viewRevision;
    uint64_t drawOrderRevision;
    std::vector<std::function<void(SDL_Event, double)>> listeners;
    std::vector<TransientAudioPlayback> transientAudioPlaybacks;
    std::vector<Audio *> pendingAudioReplays;

    bool init();
    void close();
    void updateRenderSize();
    void releaseTransientAudioPlayback(size_t index);
};
