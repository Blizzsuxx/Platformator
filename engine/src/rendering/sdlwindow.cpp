#include "rendering/sdlwindow.h"
#include "audio/audiomanager.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <SDL3/SDL_properties.h>

#include "core/gamemanager.h"
#include "core/runtimeaccess.h"
#include "platformator/sprite.h"
#include "benchmark/benchmark.h"
#include "rendering/spriterendergeometry.h"
#include "benchmark/workprobe.h"

namespace
{
bool hasDefaultRenderMapping(SDL_Renderer *renderer, int expectedWidth, int expectedHeight)
{
    if (renderer == nullptr || expectedWidth <= 0 || expectedHeight <= 0)
        return false;

    if (SDL_GetRenderTarget(renderer) != nullptr || SDL_RenderViewportSet(renderer))
        return false;

    SDL_Rect viewport{};
    if (!SDL_GetRenderViewport(renderer, &viewport) || viewport.x != 0 || viewport.y != 0 ||
        viewport.w != expectedWidth || viewport.h != expectedHeight)
        return false;

    float scaleX = 0.0f;
    float scaleY = 0.0f;
    if (!SDL_GetRenderScale(renderer, &scaleX, &scaleY) || scaleX != 1.0f || scaleY != 1.0f)
        return false;

    int logicalWidth = 0;
    int logicalHeight = 0;
    SDL_RendererLogicalPresentation logicalMode = SDL_LOGICAL_PRESENTATION_DISABLED;
    if (!SDL_GetRenderLogicalPresentation(renderer, &logicalWidth, &logicalHeight, &logicalMode) ||
        logicalMode != SDL_LOGICAL_PRESENTATION_DISABLED || logicalWidth != 0 || logicalHeight != 0)
        return false;

    int outputWidth = 0;
    int outputHeight = 0;
    int currentWidth = 0;
    int currentHeight = 0;
    if (!SDL_GetRenderOutputSize(renderer, &outputWidth, &outputHeight) ||
        !SDL_GetCurrentRenderOutputSize(renderer, &currentWidth, &currentHeight))
        return false;

    return outputWidth == expectedWidth && outputHeight == expectedHeight && currentWidth == expectedWidth &&
           currentHeight == expectedHeight;
}
} // namespace

platformator_detail::RenderCullingMode SDLWindow::resolveRequestedRenderCullingModeFromEnvironment()
{
    // This process-local control is deliberately internal. It is read while
    // SDLWindow is constructed, before Runtime publishes scene components.
    const char *value = std::getenv("PLATFORMATOR_RENDER_CULLING_MODE");
    if (value == nullptr || *value == '\0' || std::string_view(value) == "cached-linear")
        return platformator_detail::RenderCullingMode::CachedLinear;
    if (std::string_view(value) == "indexed" || std::string_view(value) == "current-linear")
        throw std::invalid_argument(
            "Retired PLATFORMATOR_RENDER_CULLING_MODE '" + std::string(value) +
            "': render grid and stateless linear culling were removed; unset PLATFORMATOR_RENDER_CULLING_MODE or set 'cached-linear' (then use WindowSettings::enableRenderCulling=false or --no-render-culling if desired)");
    throw std::invalid_argument("Unknown PLATFORMATOR_RENDER_CULLING_MODE: " + std::string(value) +
                                " (expected 'cached-linear' or empty)");
}

SDLWindow::RenderCullingPolicy SDLWindow::resolveRenderCullingPolicy(const WindowSettings &windowSettings)
{
    RenderCullingPolicy policy;
    policy.requestedMode = resolveRequestedRenderCullingModeFromEnvironment();
    policy.cullingEnabled = windowSettings.enableRenderCulling;
    policy.headless = windowSettings.headless;
    policy.effectiveMode = (!windowSettings.headless && windowSettings.enableRenderCulling)
                               ? policy.requestedMode
                               : platformator_detail::RenderCullingMode::SubmitAll;
    return policy;
}

const char *SDLWindow::renderCullingModeName(platformator_detail::RenderCullingMode mode) noexcept
{
    switch (mode)
    {
    case platformator_detail::RenderCullingMode::SubmitAll:
        return "submit-all";
    case platformator_detail::RenderCullingMode::CachedLinear:
        return "cached-linear";
    }
    return "unknown";
}

const char *SDLWindow::effectiveRenderPolicyName(const RenderCullingPolicy &policy) noexcept
{
    if (policy.headless)
        return "headless-no-rendering";
    if (!policy.cullingEnabled || policy.effectiveMode == platformator_detail::RenderCullingMode::SubmitAll)
        return "submit-all";
    return renderCullingModeName(policy.effectiveMode);
}

SDLWindow::RenderCullingPolicy SDLWindow::getRenderCullingPolicy() const noexcept
{
    RenderCullingPolicy policy;
    policy.requestedMode = requestedRenderCullingMode;
    policy.effectiveMode = effectiveRenderCullingMode;
    policy.cullingEnabled = windowSettings.enableRenderCulling;
    policy.headless = windowSettings.headless || renderer == nullptr;
    return policy;
}

SDLWindow::RenderFallbackCounts SDLWindow::getObservedRenderFallbackCounts() const noexcept
{
    return observedFallbackCounts;
}

SDLWindow::SDLWindow(const WindowSettings &windowSettings, const DebugSettings &debugSettings)
    : window(nullptr), renderer(nullptr), audioManager(nullptr), mainCamera(nullptr), sdlEvent(),
      debugDraw(DebugDraw::getInstance()), rendererName(nullptr), windowSettings(windowSettings),
      renderWidth(windowSettings.width), renderHeight(windowSettings.height), quit(false),
      paused(debugSettings.startPaused), advanceFrameRequested(false),
      requestedRenderCullingMode(resolveRequestedRenderCullingModeFromEnvironment()),
      effectiveRenderCullingMode((!windowSettings.headless && windowSettings.enableRenderCulling)
                                     ? requestedRenderCullingMode
                                     : platformator_detail::RenderCullingMode::SubmitAll),
      renderCullingState(), spriteComponents(), listeners()
{
    try
    {
#if PLATFORMATOR_ENABLE_DEBUG_TOOLS
        debugDraw.setSettings(debugSettings);
#else
        (void)debugSettings;
#endif

        if (!init())
        {
            const std::string error = SDL_GetError();
            throw std::runtime_error("Cannot initialize Platformator backends: " + error);
        }

        if (renderer != nullptr)
        {
            spriteDrawOrder.emplace();
            publishOrderCapacityObservation();
            if (effectiveRenderCullingMode == platformator_detail::RenderCullingMode::CachedLinear)
            {
                renderCullingState = std::make_unique<platformator_detail::RenderCullingState>();
                renderCullingState->publishCapacityObservation();
                PLATFORMATOR_RENDER_CULLING_ADD(renderCullingStateCreations, 1);
            }
        }

#if PLATFORMATOR_ENABLE_DEBUG_TOOLS
        listeners.push_back([this](SDL_Event event, double) {
            if (event.type != SDL_EVENT_KEY_DOWN)
            {
                return;
            }

            switch (event.key.key)
            {
            case SDLK_F1:
                debugDraw.toggleShowColliders();
                break;
            case SDLK_F2:
                debugDraw.toggleShowCollisionPoints();
                break;
            case SDLK_F3:
                debugDraw.toggleShowCollisionNormals();
                break;
            case SDLK_F4:
                debugDraw.toggleShowGridCells();
                break;
            case SDLK_F5:
                if (setPaused(!paused))
                    PLATFORMATOR_LOG("Simulation: %s\n", paused ? "paused" : "running");
                else
                    PLATFORMATOR_LOG("Could not change game pause state: %s\n", SDL_GetError());
                break;
            case SDLK_F6:
                if (paused)
                {
                    advanceFrameRequested = true;
                    PLATFORMATOR_LOG("Advancing one frame\n");
                }
                break;
            case SDLK_F9:
                debugDraw.toggleShowFps();
                break;
            default:
                break;
            }
        });
#endif
    }
    catch (...)
    {
        close();
        throw;
    }
}

SDLWindow::~SDLWindow()
{
    close();
}

bool SDLWindow::init()
{
    if (windowSettings.headless)
    {
        if (!SDL_Init(SDL_INIT_EVENTS))
            return false;
        rendererName = "Headless";
        renderWidth = std::max(1, windowSettings.width);
        renderHeight = std::max(1, windowSettings.height);
        return true;
    }
    const SDL_InitFlags initFlags = SDL_INIT_AUDIO | SDL_INIT_VIDEO;

    if (!SDL_Init(initFlags))
    {
        printf("SDL could not initialize! SDL_Error: %s", SDL_GetError());
        return false;
    }

    if (!setPaused(paused))
        return false;

    SDL_WindowFlags windowFlags = SDL_WINDOW_RESIZABLE;
    if (windowSettings.hidden)
        windowFlags |= SDL_WINDOW_HIDDEN;
    if (windowSettings.highPixelDensity)
        windowFlags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
    window = SDL_CreateWindow("Platformator", windowSettings.width, windowSettings.height, windowFlags);
    if (window == nullptr)
    {
        printf("Window could not be created! SDL_Error: %s", SDL_GetError());
        return false;
    }

    if (windowSettings.keepAspectRatio)
    {
        const float aspectRatio = static_cast<float>(windowSettings.width) / static_cast<float>(windowSettings.height);
        SDL_SetWindowAspectRatio(window, aspectRatio, aspectRatio);
    }

    renderer = SDL_CreateRenderer(window, nullptr);
    if (renderer == nullptr)
    {
        printf("Renderer could not be created! SDL_Error: %s", SDL_GetError());
        return false;
    }
    SDL_SetRenderVSync(renderer, 1);

    if (windowSettings.fullscreen)
    {
        SDL_SetWindowFullscreen(window, true);
        SDL_SyncWindow(window);
    }
    else if (windowSettings.maximizeOnStartup)
    {
        SDL_MaximizeWindow(window);
        SDL_SyncWindow(window);
    }

    updateRenderSize();

    rendererName = SDL_GetRendererName(renderer);
    if (rendererName == nullptr)
    {
        rendererName = "Unknown Renderer";
    }

    PLATFORMATOR_LOG("Renderer Used: %s\n", rendererName);
    SDL_SetWindowTitle(window, ("Platformator: " + std::string(rendererName)).c_str());

    return true;
}

void SDLWindow::close()
{
    renderCullingState.reset();
    spriteDrawOrder.reset();
    publishOrderCapacityObservation();

    if (renderer != nullptr)
    {
        SDL_DestroyRenderer(renderer);
        renderer = nullptr;
    }

    if (window != nullptr)
    {
        SDL_DestroyWindow(window);
        window = nullptr;
    }

    mainCamera = nullptr;
    rendererName = nullptr;

    SDL_Quit();
}

void SDLWindow::handleSDLEvents(double deltaTime)
{
    if (windowSettings.headless)
    {
        return;
    }

    while (SDL_PollEvent(&sdlEvent) != 0)
    {
        if (sdlEvent.type == SDL_EVENT_QUIT)
        {
            quit = true;
        }
        else if (sdlEvent.type == SDL_EVENT_WINDOW_RESIZED || sdlEvent.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
        {
            updateRenderSize();
        }

        for (const auto &listener : listeners)
        {
            listener(sdlEvent, deltaTime);
        }
    }
}

void SDLWindow::render()
{
    if (renderer == nullptr)
    {
        return;
    }
    PLATFORMATOR_BENCH_SCOPE(Render);
    if (renderCullingState)
        renderCullingState->publishCapacityObservation();
    publishOrderCapacityObservation();

    SDL_SetRenderDrawColor(renderer, windowSettings.clearColor.r, windowSettings.clearColor.g,
                           windowSettings.clearColor.b, windowSettings.clearColor.a);
    SDL_RenderClear(renderer);
    if (!mainCamera)
    {
        PLATFORMATOR_RENDER_BEFORE_PRESENT(renderer, renderWidth, renderHeight);
        if (editorSessionFrameCapture)
            editorSessionFrameCapture(renderer, renderWidth, renderHeight);
        SDL_RenderPresent(renderer);
        return;
    }

#if PLATFORMATOR_ENABLE_DEBUG_TOOLS
    PhysicsManager *physicsManager = platformator_detail::RuntimeAccess::physicsManager();
    if (shouldSimulateFrame())
    {
        for (const auto &cellEntry : physicsManager->getGrid().getCells())
        {
            debugDraw.addGridCellDebugObject(cellEntry.first);
        }
    }
#endif

#if PLATFORMATOR_ENABLE_DEBUG_TOOLS
    for (Collider *collider : physicsManager->getColliders())
    {
        if (collider != nullptr && shouldSimulateFrame())
        {
            if (collider->getColliderType() == ColliderType::BoxCollider)
            {
                debugDraw.addBoxColliderDebugObject(*(BoxCollider *)collider);
            }
            else if (collider->getColliderType() == ColliderType::CircleCollider)
            {
                debugDraw.addCircleColliderDebugObject(*(CircleCollider *)collider);
            }
        }
    }
#endif

    bool cullingEligible = false;
    if (windowSettings.enableRenderCulling)
    {
        PLATFORMATOR_RENDER_ELIGIBILITY_CHECK();
        cullingEligible = hasDefaultRenderMapping(renderer, renderWidth, renderHeight);
    }
    const auto renderView = mainCamera->prepareRenderView(renderWidth, renderHeight, cullingEligible);

    const bool useCachedSelection = renderCullingState && prepareVisibleSelection(mainCamera, renderView);
    prepareDrawOrder(useCachedSelection);
    if (useCachedSelection)
    {
        for (const auto &entry : spriteDrawOrder->entries)
            mainCamera->renderWithView(spriteComponents[entry.rank], renderer, renderView, false);
    }
    else
    {
        for (const auto &entry : spriteDrawOrder->entries)
        {
            Sprite *spriteComponent = spriteComponents[entry.rank];
            if (!spriteComponent->getGameObject()->getActive())
                continue;
            mainCamera->renderWithView(spriteComponent, renderer, renderView);
        }
    }

#if PLATFORMATOR_ENABLE_DEBUG_TOOLS
    debugDraw.render(renderer, mainCamera, renderWidth, renderHeight);
#endif
    PLATFORMATOR_RENDER_BEFORE_PRESENT(renderer, renderWidth, renderHeight);
    if (editorSessionFrameCapture)
        editorSessionFrameCapture(renderer, renderWidth, renderHeight);
    SDL_RenderPresent(renderer);
}

void SDLWindow::clearDebugObjects()
{
#if PLATFORMATOR_ENABLE_DEBUG_TOOLS
    debugDraw.clearDebugObjects();
#endif
}

SDL_Window *SDLWindow::getWindow() const
{
    return window;
}

SDL_Renderer *SDLWindow::getRenderer() const
{
    return renderer;
}

bool SDLWindow::attachAudioManager(AudioManager *manager) noexcept
{
    audioManager = manager;
    if (audioManager == nullptr)
    {
        return true;
    }
    return setPaused(paused);
}

int SDLWindow::getRenderWidth() const
{
    return renderWidth;
}

int SDLWindow::getRenderHeight() const
{
    return renderHeight;
}

bool SDLWindow::isHeadless() const
{
    return windowSettings.headless;
}

SDL_Color SDLWindow::getClearColor() const noexcept
{
    return windowSettings.clearColor;
}

void SDLWindow::setClearColor(const SDL_Color &color) noexcept
{
    windowSettings.clearColor = color;
}

void SDLWindow::setClearColor(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) noexcept
{
    windowSettings.clearColor = SDL_Color{r, g, b, a};
}

bool SDLWindow::isRunning() const
{
    return !quit;
}

bool SDLWindow::shouldUseFixedStepDelta() const
{
#if PLATFORMATOR_ENABLE_DEBUG_TOOLS
    return paused;
#else
    return editorSessionControlsEnabled && paused;
#endif
}

bool SDLWindow::shouldSimulateFrame() const
{
    if (editorSessionControlsEnabled)
        return !paused || advanceFrameRequested;
#if PLATFORMATOR_ENABLE_DEBUG_TOOLS
    return !paused || advanceFrameRequested;
#else
    return true;
#endif
}

void SDLWindow::clearAdvanceFrameRequest()
{
#if PLATFORMATOR_ENABLE_DEBUG_TOOLS
    advanceFrameRequested = false;
#else
    if (editorSessionControlsEnabled)
        advanceFrameRequested = false;
#endif
}

bool SDLWindow::enableEditorSessionControls() noexcept
{
    editorSessionControlsEnabled = true;
    if (!setPaused(paused))
    {
        editorSessionControlsEnabled = false;
        return false;
    }
    return true;
}

bool SDLWindow::setEditorSessionPaused(bool value) noexcept
{
    if (!editorSessionControlsEnabled)
        return false;
    return setPaused(value);
}

bool SDLWindow::setPaused(bool value) noexcept
{
    bool pauseAudio = value;
#if !PLATFORMATOR_ENABLE_DEBUG_TOOLS
    pauseAudio = editorSessionControlsEnabled && value;
#endif
    if (audioManager != nullptr && !audioManager->setPaused(pauseAudio))
    {
        return false;
    }
    paused = value;
    advanceFrameRequested = false;
    return true;
}

bool SDLWindow::requestEditorSessionStep() noexcept
{
    if (!editorSessionControlsEnabled || !paused || advanceFrameRequested)
        return false;
    advanceFrameRequested = true;
    return true;
}

bool SDLWindow::isEditorSessionPaused() const noexcept
{
    return editorSessionControlsEnabled && paused;
}

void SDLWindow::setEditorSessionFrameCapture(
    std::function<void(SDL_Renderer *, int, int)> callback)
{
    editorSessionFrameCapture = std::move(callback);
}

void SDLWindow::setMainCamera(Camera *mainCamera)
{
    this->mainCamera = mainCamera;
}

Camera *SDLWindow::getMainCamera() const
{
    return mainCamera;
}

void SDLWindow::updateRenderSize()
{
    if (renderer != nullptr && SDL_GetRenderOutputSize(renderer, &renderWidth, &renderHeight))
    {
        renderWidth = std::max(1, renderWidth);
        renderHeight = std::max(1, renderHeight);
        return;
    }

    if (window != nullptr && SDL_GetWindowSizeInPixels(window, &renderWidth, &renderHeight))
    {
        renderWidth = std::max(1, renderWidth);
        renderHeight = std::max(1, renderHeight);
    }
}
