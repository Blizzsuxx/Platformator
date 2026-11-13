#pragma once

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <array>
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
    void setEditorKey(SDL_Scancode key, bool down) noexcept
    {
        if (key > SDL_SCANCODE_UNKNOWN && key < SDL_SCANCODE_COUNT)
            editorKeys[static_cast<std::size_t>(key)] = down;
    }
    void releaseEditorInput() noexcept { editorKeys.fill(false); editorMouseButtons = 0; editorMouseActive = false; }
    bool isKeyDown(SDL_Scancode key) const noexcept
    {
        return key > SDL_SCANCODE_UNKNOWN && key < SDL_SCANCODE_COUNT &&
            (editorKeys[static_cast<std::size_t>(key)] || SDL_GetKeyboardState(nullptr)[key]);
    }
    void setEditorMouse(float x, float y, int button = 0, bool down = false) noexcept
    {
        editorMouseX = x; editorMouseY = y; editorMouseActive = true;
        if (button > 0 && button <= 5)
        {
            if (down) editorMouseButtons |= SDL_BUTTON_MASK(button);
            else editorMouseButtons &= ~SDL_BUTTON_MASK(button);
        }
    }
    SDL_MouseButtonFlags getMouseState(float *x, float *y) const noexcept
    {
        if (!editorMouseActive) return SDL_GetMouseState(x, y);
        if (x) *x = editorMouseX;
        if (y) *y = editorMouseY;
        return editorMouseButtons;
    }
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
    void enableEditorSessionControls() noexcept;
    bool setEditorSessionPaused(bool paused) noexcept;
    bool requestEditorSessionStep() noexcept;
    bool isEditorSessionPaused() const noexcept;
    void setEditorSessionFrameCapture(std::function<void(SDL_Renderer *, int, int)> callback);
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

    std::array<bool, SDL_SCANCODE_COUNT> editorKeys{};
    SDL_MouseButtonFlags editorMouseButtons = 0;
    float editorMouseX = 0.0f, editorMouseY = 0.0f;
    bool editorMouseActive = false;
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
    bool editorSessionControlsEnabled = false;
    std::function<void(SDL_Renderer *, int, int)> editorSessionFrameCapture;
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
