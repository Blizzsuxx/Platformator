#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "platformator/camera.h"
#include "platformator/debugsettings.h"
#include "rendering/debugdraw.h"
#include "rendering/renderculling.h"
#include "platformator/windowsettings.h"

#ifndef PLATFORMATOR_ENABLE_BENCHMARK_ORDER_MODES
#define PLATFORMATOR_ENABLE_BENCHMARK_ORDER_MODES 0
#endif

class AudioManager;
class Sprite;

class SDLWindow
{
  public:
    struct RenderCullingPolicy
    {
        platformator_detail::RenderCullingMode requestedMode = platformator_detail::RenderCullingMode::CachedLinear;
        platformator_detail::RenderCullingMode effectiveMode = platformator_detail::RenderCullingMode::CachedLinear;
        bool cullingEnabled = true;
        bool headless = false;
    };

#if PLATFORMATOR_ENABLE_BENCHMARK_ORDER_MODES
    enum class BenchmarkSpriteOrderMode
    {
        DefaultRebuild,
        ForcedFullSort,
        SingleEntryRepair
    };

    struct BenchmarkOrderTiming
    {
        std::uint64_t calls = 0;
        std::uint64_t totalNs = 0;
        std::uint64_t lastNs = 0;
    };
#endif

    struct RenderFallbackCounts
    {
        std::size_t mappingFallbacks = 0;
        std::size_t numericFallbacks = 0;
    };

    SDLWindow(const WindowSettings &windowSettings, const DebugSettings &debugSettings);
    ~SDLWindow();

    static platformator_detail::RenderCullingMode resolveRequestedRenderCullingModeFromEnvironment();
    static RenderCullingPolicy resolveRenderCullingPolicy(const WindowSettings &windowSettings);
    static const char *renderCullingModeName(platformator_detail::RenderCullingMode mode) noexcept;
    static const char *effectiveRenderPolicyName(const RenderCullingPolicy &policy) noexcept;
    RenderCullingPolicy getRenderCullingPolicy() const noexcept;
    RenderFallbackCounts getObservedRenderFallbackCounts() const noexcept;

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
    bool attachAudioManager(AudioManager *audioManager) noexcept;
    int getRenderWidth() const;
    int getRenderHeight() const;
    bool isHeadless() const;
    SDL_Color getClearColor() const noexcept;
    void setClearColor(const SDL_Color &color) noexcept;
    void setClearColor(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255) noexcept;

    void addSpriteComponent(Sprite *spriteComponent);
    void removeSpriteComponent(Sprite *spriteComponent);
    void notifySpriteGeometryChanged(Sprite *spriteComponent) noexcept;
    void notifySpriteOrderChanged(size_t slot) noexcept;
#if PLATFORMATOR_ENABLE_BENCHMARK_ORDER_MODES
    void setBenchmarkSpriteOrderMode(BenchmarkSpriteOrderMode mode) noexcept;
    BenchmarkOrderTiming consumeBenchmarkOrderTiming() noexcept;
    BenchmarkOrderTiming consumeBenchmarkSelectionTiming() noexcept;
#endif
    void releaseRenderingStorage() noexcept;
    void clearSceneComponents() noexcept;

    bool shouldSimulateFrame() const;
    bool shouldUseFixedStepDelta() const;
    void clearAdvanceFrameRequest();
    bool enableEditorSessionControls() noexcept;
    bool setEditorSessionPaused(bool paused) noexcept;
    bool requestEditorSessionStep() noexcept;
    bool isEditorSessionPaused() const noexcept;
    void setEditorSessionFrameCapture(std::function<void(SDL_Renderer *, int, int)> callback);
    void clearDebugObjects();
    Camera *getMainCamera() const;

  private:
    struct SpriteDrawEntry
    {
        size_t rank = 0;
        std::int32_t zOrder = 0;
        uint64_t registrationSequence = 0;
    };

    struct SpriteDrawOrder
    {
        std::vector<uint64_t> sequenceBySlot;
        std::vector<SpriteDrawEntry> entries;
        uint64_t nextSequence = 1;
        uint64_t preparedMembershipRevision = 0;
#if PLATFORMATOR_ENABLE_BENCHMARK_ORDER_MODES
        BenchmarkSpriteOrderMode orderMode = BenchmarkSpriteOrderMode::DefaultRebuild;
        size_t singleEditSlot = 0;
        BenchmarkOrderTiming benchmarkTiming{};
        bool singleEditCandidateValid = false;
#endif
        bool preparedOnce = false;
        bool orderDirty = true;
        bool preparedAll = false;
    };

    std::array<bool, SDL_SCANCODE_COUNT> editorKeys{};
    SDL_MouseButtonFlags editorMouseButtons = 0;
    float editorMouseX = 0.0f, editorMouseY = 0.0f;
    bool editorMouseActive = false;
    SDL_Window *window;
    SDL_Renderer *renderer;
    AudioManager *audioManager = nullptr;
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
    RenderFallbackCounts observedFallbackCounts{};
    std::unique_ptr<platformator_detail::RenderCullingState> renderCullingState;
    std::optional<SpriteDrawOrder> spriteDrawOrder;
#if PLATFORMATOR_ENABLE_BENCHMARK_ORDER_MODES
    BenchmarkOrderTiming benchmarkSelectionTiming{};
#endif

    std::vector<Sprite *> spriteComponents;
    std::vector<std::function<void(SDL_Event, double)>> listeners;

    bool init();
    bool setPaused(bool value) noexcept;
    void close();
    void updateRenderSize();
    bool prepareVisibleSelection(Camera *camera, const platformator_detail::SpriteRenderView &view);
    void prepareDrawOrder(bool useCachedSelection);
    void publishOrderCapacityObservation() const noexcept;
};
