#include "sdlwindow.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <SDL3/SDL_properties.h>

#include "platformator/audiowrapper.h"
#include "platformator/audio.h"
#include "gamemanager.h"
#include "runtimeaccess.h"
#include "platformator/sprite.h"
#include "benchmark.h"
#include "spriterendergeometry.h"
#include "workprobe.h"

namespace
{
#if PLATFORMATOR_ENABLE_BENCHMARK_ORDER_MODES
struct ScopedBenchmarkTiming
{
    SDLWindow::BenchmarkOrderTiming &timing;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

    ~ScopedBenchmarkTiming() noexcept
    {
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
        const std::uint64_t ns = elapsed > 0 ? static_cast<std::uint64_t>(elapsed) : 0;
        ++timing.calls;
        timing.lastNs = ns;
        timing.totalNs += ns;
    }
};
#endif

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

bool overlaps(const platformator_detail::SpriteRenderBounds &a,
              const platformator_detail::SpriteRenderBounds &b)
{
    return a.maxX >= b.minX && a.minX <= b.maxX && a.maxY >= b.minY && a.minY <= b.maxY;
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
    : window(nullptr), renderer(nullptr), mixer(nullptr), mainCamera(nullptr), sdlEvent(),
      debugDraw(DebugDraw::getInstance()), rendererName(nullptr), windowSettings(windowSettings),
      renderWidth(windowSettings.width), renderHeight(windowSettings.height), quit(false),
      paused(debugSettings.startPaused), advanceFrameRequested(false),
      requestedRenderCullingMode(resolveRequestedRenderCullingModeFromEnvironment()),
      effectiveRenderCullingMode((!windowSettings.headless && windowSettings.enableRenderCulling)
                                     ? requestedRenderCullingMode
                                     : platformator_detail::RenderCullingMode::SubmitAll),
      renderCullingState(), spriteComponents(), listeners(), transientAudioPlaybacks()
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

    if (!MIX_Init())
    {
        printf("SDL_mixer could not initialize! SDL_mixer Error: %s", SDL_GetError());
        return false;
    }

    mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (mixer == nullptr)
    {
        printf("SDL_mixer could not create a mixer device! SDL_mixer Error: %s", SDL_GetError());
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
    clearTransientAudio();
    renderCullingState.reset();
    spriteDrawOrder.reset();
    publishOrderCapacityObservation();

    if (mixer != nullptr)
    {
        MIX_DestroyMixer(mixer);
        mixer = nullptr;
    }

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

    MIX_Quit();
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

MIX_Mixer *SDLWindow::getMixer() const
{
    return mixer;
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

bool SDLWindow::prepareVisibleSelection(Camera *camera, const platformator_detail::SpriteRenderView &view)
{
    using namespace platformator_detail;
#if PLATFORMATOR_ENABLE_BENCHMARK_ORDER_MODES
    ScopedBenchmarkTiming scopedSelectionTiming{benchmarkSelectionTiming};
#endif
    if (!renderCullingState || camera == nullptr)
        return false;
    if (!view.cullingEligible)
    {
        ++observedFallbackCounts.mappingFallbacks;
        PLATFORMATOR_RENDER_CULLING_ADD(renderCullingMappingFallbacks, 1);
        return false;
    }

    auto &state = *renderCullingState;
    const SpriteRenderViewKey &cachedView = state.viewKey();
    if (state.selectionValid() && state.dirtyCount() == 0 && cachedView.valid &&
        cachedView.cameraHandle == camera->getHandle() && cachedView.cameraX == view.cameraX &&
        cachedView.cameraY == view.cameraY && cachedView.projection.valid &&
        cachedView.projection.scaleX == view.scaleX && cachedView.projection.scaleY == view.scaleY &&
        cachedView.projection.outputWidth == view.outputWidth &&
        cachedView.projection.outputHeight == view.outputHeight)
    {
        PLATFORMATOR_RENDER_CULLING_ADD(renderCullingCacheHits, 1);
        return true;
    }

    PLATFORMATOR_RENDER_CULLING_ADD(renderCullingViewQueryBuilds, 1);
    const SpriteRenderViewKey currentView = makeSpriteRenderViewKey(camera->getHandle(), view);
    SpriteRenderBounds query;
    if (!currentView.valid || !makeCertifiedRenderViewQuery(view, query))
    {
        ++observedFallbackCounts.numericFallbacks;
        PLATFORMATOR_RENDER_CULLING_ADD(renderCullingNumericFallbacks, 1);
        return false;
    }

    const bool projectionChanged = !sameSpriteRenderProjection(state.projectionKey(), currentView.projection);
    const bool viewChanged = !sameSpriteRenderView(state.viewKey(), currentView);
    const bool rebuildSelection = projectionChanged || viewChanged || !state.selectionValid();
    if (projectionChanged)
        PLATFORMATOR_RENDER_CULLING_ADD(renderCullingProjectionRebuilds, 1);

    GameManager &manager = RuntimeAccess::gameManager();
    const auto resolveRecordSprite = [&](const RenderCullingState::Record &record) {
        PLATFORMATOR_RENDER_CULLING_ADD(renderCullingHandleResolutions, 1);
        return static_cast<Sprite *>(manager.resolveHandle(record.spriteHandle, ObjectKind::Sprite));
    };

    const auto refreshRecord = [&](RenderCullingState::RecordIndex index) -> Sprite * {
        auto &record = state.recordAt(index);
        Sprite *sprite = resolveRecordSprite(record);
        if (sprite == nullptr || sprite->getTexture() == nullptr)
        {
            record.boundsValid = false;
            PLATFORMATOR_RENDER_CULLING_ADD(renderCullingBoundsRefreshes, 1);
            return sprite;
        }

        const Eigen::Vector2f &position = camera->getPublishedSpritePosition(sprite);
        PLATFORMATOR_RENDER_CULLING_ADD(renderCullingProxyCertifications, 1);
        record.boundsValid = makeCertifiedSpriteRenderProxy(position.x(), position.y(), sprite->getWidth(),
                                                            sprite->getHeight(),
                                                            sprite->getGameObject()->getRotationInDegrees(),
                                                            view.scaleX, view.scaleY, record.bounds);
        PLATFORMATOR_RENDER_CULLING_ADD(renderCullingBoundsRefreshes, 1);
        return sprite;
    };

    const auto visibleWithResolvedSprite = [&](RenderCullingState::RecordIndex rank, Sprite *resolvedSprite,
                                               bool hasResolvedSprite) {
        const auto &record = state.recordAt(rank);
        PLATFORMATOR_RENDER_CULLING_ADD(renderCullingCandidateVisits, 1);
        if (!record.boundsValid)
            return true;
        if (!overlaps(record.bounds, query))
            return false;

        Sprite *sprite = hasResolvedSprite ? resolvedSprite : resolveRecordSprite(record);
        if (sprite == nullptr)
            return true;
        SDL_FRect destination{};
        float angleDegrees = 0.0f;
        camera->prepareSpriteGeometry(sprite, view, destination, angleDegrees);
        PLATFORMATOR_RENDER_VISIBILITY_CHECK();
        return classifySpriteForRender(destination, angleDegrees, view) != SpriteRenderClassification::Reject;
    };

    if (rebuildSelection)
    {
        if (projectionChanged)
        {
            for (RenderCullingState::RecordIndex rank = 0; rank < state.recordCount(); ++rank)
                refreshRecord(rank);
        }
        else
        {
            const auto queuedCount = state.dirtyCount();
            for (RenderCullingState::RecordIndex queued = 0; queued < queuedCount; ++queued)
                refreshRecord(state.dirtyAt(queued));
        }

        state.beginVisibleSelectionScratch();
        for (RenderCullingState::RecordIndex rank = 0; rank < state.recordCount(); ++rank)
        {
            PLATFORMATOR_RENDER_CULLING_ADD(renderCullingFullPopulationVisits, 1);
            if (visibleWithResolvedSprite(rank, nullptr, false))
                state.appendVisibleSelectionScratch(rank);
        }
        state.publishVisibleSelectionScratch();
    }
    else
    {
        const auto queuedCount = state.dirtyCount();
        assert(queuedCount > 0);
        state.beginVisibleDelta();
        for (RenderCullingState::RecordIndex queued = 0; queued < queuedCount; ++queued)
        {
            const auto rank = state.dirtyAt(queued);
            Sprite *sprite = refreshRecord(rank);
            state.updateVisibleRank(rank, visibleWithResolvedSprite(rank, sprite, true));
        }
        state.finishVisibleDelta();
    }

    state.setPreparedViewKey(currentView);
    state.clearDirtyQueue();
    return true;
}

void SDLWindow::prepareDrawOrder(bool useCachedSelection)
{
    if (!spriteDrawOrder)
        return;

    auto &order = *spriteDrawOrder;
#if PLATFORMATOR_ENABLE_BENCHMARK_ORDER_MODES
    ScopedBenchmarkTiming scopedTiming{order.benchmarkTiming};
#endif

    const size_t population = spriteComponents.size();
    const bool currentAll = !useCachedSelection || (renderCullingState->visibleRanks().size() == population);
    const uint64_t currentRevision = useCachedSelection ? renderCullingState->visibleMembershipRevision() : 0;
    const bool membershipChanged = !order.preparedOnce || (currentAll != order.preparedAll) ||
                                   (!currentAll && order.preparedMembershipRevision != currentRevision);

    const auto entryLess = [](const SpriteDrawEntry &left, const SpriteDrawEntry &right) {
        PLATFORMATOR_RENDER_CULLING_ADD(spriteOrderComparisons, 1);
        if (left.zOrder != right.zOrder)
            return left.zOrder < right.zOrder;
        return left.registrationSequence < right.registrationSequence;
    };

#if PLATFORMATOR_ENABLE_BENCHMARK_ORDER_MODES
    if (order.orderMode != BenchmarkSpriteOrderMode::ForcedFullSort && !order.orderDirty && !membershipChanged)
        return;

    if (order.orderMode == BenchmarkSpriteOrderMode::SingleEntryRepair && order.orderDirty && !membershipChanged &&
        order.singleEditCandidateValid && order.singleEditSlot < population)
    {
        const size_t targetRank = order.singleEditSlot;
        size_t oldPos = order.entries.size();
        size_t lookupVisits = 0;
        for (size_t idx = 0; idx < order.entries.size(); ++idx)
        {
            ++lookupVisits;
            if (order.entries[idx].rank == targetRank)
            {
                oldPos = idx;
                break;
            }
        }
        PLATFORMATOR_RENDER_CULLING_ADD(spriteOrderLookupVisits, lookupVisits);
        if (oldPos < order.entries.size())
        {
            const SpriteDrawEntry updated{targetRank, spriteComponents[targetRank]->getZOrder(),
                                          order.sequenceBySlot[targetRank]};
            size_t moves = 0;
            if (oldPos > 0 && entryLess(updated, order.entries[oldPos - 1]))
            {
                auto it = std::lower_bound(order.entries.begin(),
                                           order.entries.begin() + static_cast<std::ptrdiff_t>(oldPos), updated,
                                           entryLess);
                const size_t newPos = static_cast<size_t>(it - order.entries.begin());
                std::move_backward(it, order.entries.begin() + static_cast<std::ptrdiff_t>(oldPos),
                                   order.entries.begin() + static_cast<std::ptrdiff_t>(oldPos + 1));
                order.entries[newPos] = updated;
                moves = oldPos - newPos;
            }
            else if (oldPos + 1 < order.entries.size() && entryLess(order.entries[oldPos + 1], updated))
            {
                auto it = std::lower_bound(order.entries.begin() + static_cast<std::ptrdiff_t>(oldPos + 1),
                                           order.entries.end(), updated, entryLess);
                const size_t newPos = static_cast<size_t>(it - order.entries.begin()) - 1;
                std::move(order.entries.begin() + static_cast<std::ptrdiff_t>(oldPos + 1), it,
                          order.entries.begin() + static_cast<std::ptrdiff_t>(oldPos));
                order.entries[newPos] = updated;
                moves = newPos - oldPos;
            }
            else
            {
                order.entries[oldPos] = updated;
            }
            PLATFORMATOR_RENDER_CULLING_ADD(spriteOrderPreparations, 1);
            PLATFORMATOR_RENDER_CULLING_ADD(spriteOrderEntries, 1);
            PLATFORMATOR_RENDER_CULLING_ADD(spriteOrderRepairs, 1);
            PLATFORMATOR_RENDER_CULLING_ADD(spriteOrderMoves, moves);
            order.orderDirty = false;
            order.singleEditCandidateValid = false;
            return;
        }
    }
#else
    if (!order.orderDirty && !membershipChanged)
        return;
#endif

    PLATFORMATOR_RENDER_CULLING_ADD(spriteOrderPreparations, 1);
    order.entries.clear();
    if (useCachedSelection)
    {
        for (const size_t rank : renderCullingState->visibleRanks())
        {
            Sprite *sprite = spriteComponents[rank];
            order.entries.push_back({rank, sprite->getZOrder(), order.sequenceBySlot[rank]});
        }
    }
    else
    {
        for (size_t rank = 0; rank < population; ++rank)
        {
            Sprite *sprite = spriteComponents[rank];
            order.entries.push_back({rank, sprite->getZOrder(), order.sequenceBySlot[rank]});
        }
    }
    PLATFORMATOR_RENDER_CULLING_ADD(spriteOrderEntries, order.entries.size());

    if (order.entries.size() > 1)
    {
        // EFFICIENCY: Retained Z edits and membership changes perform a full O(V log V)
        // rebuild-and-sort over active visible slots. SingleEntryRepair exists behind
        // BenchmarkSpriteOrderMode for bounded comparison, but pilot timing under
        // software vsync=1 was inconclusive within run variation (median -0.001 ms,
        // stdev 0.065 ms vs ~7.4k shifted entries per edit), so the simpler full
        // rebuild remains the production default.
        PLATFORMATOR_RENDER_CULLING_ADD(spriteOrderSorts, 1);
        std::sort(order.entries.begin(), order.entries.end(), entryLess);
    }

    order.preparedAll = currentAll;
    order.preparedMembershipRevision = currentAll ? 0 : currentRevision;
    order.preparedOnce = true;
    order.orderDirty = false;
#if PLATFORMATOR_ENABLE_BENCHMARK_ORDER_MODES
    order.singleEditCandidateValid = false;
#endif
}

void SDLWindow::publishOrderCapacityObservation() const noexcept
{
#if PLATFORMATOR_ENABLE_WORK_PROBES
    if (spriteDrawOrder)
    {
        PLATFORMATOR_RENDER_CULLING_SET(spriteOrderOwnedRecords, spriteDrawOrder->sequenceBySlot.size());
        PLATFORMATOR_RENDER_CULLING_SET(spriteOrderOwnedEntryCapacity, spriteDrawOrder->entries.capacity());
        const size_t bytes =
            spriteDrawOrder->sequenceBySlot.capacity() * sizeof(uint64_t) +
            spriteDrawOrder->entries.capacity() * sizeof(SpriteDrawEntry);
        PLATFORMATOR_RENDER_CULLING_SET(spriteOrderOwnedBytes, bytes);
    }
    else
    {
        PLATFORMATOR_RENDER_CULLING_SET(spriteOrderOwnedRecords, 0);
        PLATFORMATOR_RENDER_CULLING_SET(spriteOrderOwnedEntryCapacity, 0);
        PLATFORMATOR_RENDER_CULLING_SET(spriteOrderOwnedBytes, 0);
    }
#endif
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
    if (mixer != nullptr)
    {
        const auto device = static_cast<SDL_AudioDeviceID>(SDL_GetNumberProperty(
            MIX_GetMixerProperties(mixer), MIX_PROP_MIXER_DEVICE_NUMBER, 0));
        // Suspend this mixer's logical device, keeping individual track states
        // intact. This also freezes fades and tracks started during game pause.
        if (device == 0)
            return SDL_SetError("Game mixer has no audio playback device");
        if (!(pauseAudio ? SDL_PauseAudioDevice(device) : SDL_ResumeAudioDevice(device)))
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

void SDLWindow::addSpriteComponent(Sprite *spriteComponent)
{
    if (spriteComponent == nullptr || spriteComponent->getIsRegisteredInWindow() ||
        !spriteComponent->getGameObject()->getActive())
    {
        return;
    }

    const size_t newIndex = spriteComponents.size();
    const size_t required = newIndex + 1;
    if (required > spriteComponents.capacity())
    {
        const size_t doubled = spriteComponents.capacity() > std::numeric_limits<size_t>::max() / 2
                                   ? std::numeric_limits<size_t>::max()
                                   : spriteComponents.capacity() * 2;
        const size_t target = std::max(required, std::max<size_t>(4, doubled));
        spriteComponents.reserve(target);
        if (spriteDrawOrder)
        {
            spriteDrawOrder->sequenceBySlot.reserve(target);
            spriteDrawOrder->entries.reserve(target);
        }
    }
    else if (spriteDrawOrder)
    {
        if (spriteDrawOrder->sequenceBySlot.capacity() < spriteComponents.capacity())
            spriteDrawOrder->sequenceBySlot.reserve(spriteComponents.capacity());
        if (spriteDrawOrder->entries.capacity() < spriteComponents.capacity())
            spriteDrawOrder->entries.reserve(spriteComponents.capacity());
    }
    if (renderCullingState)
        renderCullingState->appendRecord(spriteComponent->getHandle());
    if (spriteDrawOrder)
    {
        spriteDrawOrder->sequenceBySlot.push_back(spriteDrawOrder->nextSequence++);
        spriteDrawOrder->entries.clear();
        spriteDrawOrder->orderDirty = true;
#if PLATFORMATOR_ENABLE_BENCHMARK_ORDER_MODES
        spriteDrawOrder->singleEditCandidateValid = false;
#endif
        spriteDrawOrder->preparedAll = false;
    }
    spriteComponents.push_back(spriteComponent);
    spriteComponent->setWindowIndex(newIndex);
    spriteComponent->setIsRegisteredInWindow(true);
    publishOrderCapacityObservation();
}

void SDLWindow::removeSpriteComponent(Sprite *spriteComponent)
{
    if (spriteComponent == nullptr || !spriteComponent->getIsRegisteredInWindow())
    {
        return;
    }

    size_t removeIndex = spriteComponent->getWindowIndex();
    size_t lastIndex = spriteComponents.size() - 1;

    if (spriteDrawOrder)
    {
        spriteDrawOrder->entries.clear();
        spriteDrawOrder->orderDirty = true;
#if PLATFORMATOR_ENABLE_BENCHMARK_ORDER_MODES
        spriteDrawOrder->singleEditCandidateValid = false;
#endif
        spriteDrawOrder->preparedAll = false;
    }

    if (renderCullingState)
        renderCullingState->removeRecord(removeIndex);

    if (removeIndex != lastIndex)
    {
        Sprite *movedSprite = spriteComponents[lastIndex];
        spriteComponents[removeIndex] = movedSprite;
        movedSprite->setWindowIndex(removeIndex);
        if (spriteDrawOrder)
            spriteDrawOrder->sequenceBySlot[removeIndex] = spriteDrawOrder->sequenceBySlot[lastIndex];
    }

    if (spriteDrawOrder)
        spriteDrawOrder->sequenceBySlot.pop_back();

    spriteComponents.pop_back();
    spriteComponent->setWindowIndex(SIZE_MAX);
    spriteComponent->setIsRegisteredInWindow(false);
    publishOrderCapacityObservation();
}

void SDLWindow::notifySpriteGeometryChanged(Sprite *spriteComponent) noexcept
{
    if (!renderCullingState || spriteComponent == nullptr || !spriteComponent->getIsRegisteredInWindow())
        return;

    renderCullingState->markDirty(spriteComponent->getWindowIndex(), spriteComponent->getHandle());
}

void SDLWindow::notifySpriteOrderChanged(size_t slot) noexcept
{
    if (!spriteDrawOrder)
        return;
    auto &order = *spriteDrawOrder;
#if PLATFORMATOR_ENABLE_BENCHMARK_ORDER_MODES
    if (order.orderDirty && order.orderMode != BenchmarkSpriteOrderMode::SingleEntryRepair)
        return;
#else
    if (order.orderDirty)
        return;
#endif
    const bool stale =
        slot >= spriteComponents.size() || !order.preparedOnce ||
        (!order.preparedAll && (!renderCullingState || !renderCullingState->selectionValid() ||
                                order.preparedMembershipRevision != renderCullingState->visibleMembershipRevision()));
    if (stale)
    {
#if PLATFORMATOR_ENABLE_BENCHMARK_ORDER_MODES
        order.singleEditCandidateValid = false;
#endif
        order.orderDirty = true;
        return;
    }
    const bool affected = order.preparedAll || renderCullingState->isRankVisible(slot);
    if (!affected)
        return;

#if PLATFORMATOR_ENABLE_BENCHMARK_ORDER_MODES
    if (order.orderMode == BenchmarkSpriteOrderMode::SingleEntryRepair)
    {
        if (!order.orderDirty && !order.entries.empty())
        {
            order.singleEditCandidateValid = true;
            order.singleEditSlot = slot;
        }
        else if (!order.singleEditCandidateValid || order.singleEditSlot != slot)
        {
            order.singleEditCandidateValid = false;
        }
    }
#endif
    order.orderDirty = true;
}

#if PLATFORMATOR_ENABLE_BENCHMARK_ORDER_MODES
void SDLWindow::setBenchmarkSpriteOrderMode(BenchmarkSpriteOrderMode mode) noexcept
{
    if (!spriteDrawOrder || spriteDrawOrder->orderMode == mode)
        return;
    spriteDrawOrder->orderMode = mode;
    spriteDrawOrder->singleEditCandidateValid = false;
    spriteDrawOrder->orderDirty = true;
}

SDLWindow::BenchmarkOrderTiming SDLWindow::consumeBenchmarkOrderTiming() noexcept
{
    if (!spriteDrawOrder)
        return {};
    const BenchmarkOrderTiming current = spriteDrawOrder->benchmarkTiming;
    spriteDrawOrder->benchmarkTiming = {};
    return current;
}

SDLWindow::BenchmarkOrderTiming SDLWindow::consumeBenchmarkSelectionTiming() noexcept
{
    const BenchmarkOrderTiming current = benchmarkSelectionTiming;
    benchmarkSelectionTiming = {};
    return current;
}
#endif

void SDLWindow::releaseRenderingStorage() noexcept
{
    if (renderCullingState)
        renderCullingState->releaseStorage();
#if PLATFORMATOR_ENABLE_BENCHMARK_ORDER_MODES
    benchmarkSelectionTiming = {};
#endif
    if (spriteDrawOrder)
    {
        std::vector<uint64_t>().swap(spriteDrawOrder->sequenceBySlot);
        std::vector<SpriteDrawEntry>().swap(spriteDrawOrder->entries);
        spriteDrawOrder->nextSequence = 1;
        spriteDrawOrder->preparedMembershipRevision = 0;
#if PLATFORMATOR_ENABLE_BENCHMARK_ORDER_MODES
        spriteDrawOrder->singleEditSlot = 0;
        spriteDrawOrder->benchmarkTiming = {};
        spriteDrawOrder->singleEditCandidateValid = false;
#endif
        spriteDrawOrder->preparedOnce = false;
        spriteDrawOrder->orderDirty = true;
        spriteDrawOrder->preparedAll = false;
    }
    publishOrderCapacityObservation();
}

void SDLWindow::clearSceneComponents() noexcept
{
    for (Sprite *sprite : spriteComponents)
    {
        sprite->setIsRegisteredInWindow(false);
        sprite->setWindowIndex(SIZE_MAX);
    }
    spriteComponents.clear();
    mainCamera = nullptr;
    releaseRenderingStorage();
}

void SDLWindow::setMainCamera(Camera *mainCamera)
{
    this->mainCamera = mainCamera;
}

void SDLWindow::addSdlListener(const std::function<void(SDL_Event, double)> &listener)
{
    listeners.push_back(listener);
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

bool SDLWindow::playAndForget(AudioWrapper *audioWrapper, float gain, int loopCount)
{
    if (audioWrapper == nullptr || mixer == nullptr)
    {
        return false;
    }

    std::unique_ptr<MIX_Track, decltype(&MIX_DestroyTrack)> track(MIX_CreateTrack(mixer), MIX_DestroyTrack);

    if (!track || !MIX_SetTrackAudio(track.get(), audioWrapper->getAudio()))
    {
        return false;
    }

    MIX_SetTrackGain(track.get(), std::max(0.0f, gain));

    SDL_PropertiesID options = 0;
    if (loopCount != 0)
    {
        options = SDL_CreateProperties();
        if (options == 0 || !SDL_SetNumberProperty(options, MIX_PROP_PLAY_LOOPS_NUMBER, loopCount))
        {
            if (options != 0)
            {
                SDL_DestroyProperties(options);
            }

            return false;
        }
    }

    const bool played = MIX_PlayTrack(track.get(), options);
    if (options != 0)
    {
        SDL_DestroyProperties(options);
    }

    if (!played)
    {
        return false;
    }

    transientAudioPlaybacks.push_back({track.get(), audioWrapper});
    audioWrapper->addReference();
    track.release();
    return true;
}

void SDLWindow::updateAudio()
{
    size_t index = 0;
    while (index < transientAudioPlaybacks.size())
    {
        MIX_Track *track = transientAudioPlaybacks[index].track;
        if (!MIX_TrackPlaying(track))
        {
            releaseTransientAudioPlayback(index);
            continue;
        }

        index++;
    }

    index = 0;
    while (index < pendingAudioReplays.size())
    {
        Audio *audio = pendingAudioReplays[index];
        auto *owner = audio->getGameObject();
        if (audio->isRetired() || (owner && (owner->isRetired() || !owner->getActive())))
        {
            audio->cancelPendingReplay();
            continue;
        }
        if (shouldUseFixedStepDelta() || audio->isPlaying() || audio->isPaused())
        {
            ++index;
            continue;
        }
        pendingAudioReplays[index] = pendingAudioReplays.back();
        pendingAudioReplays.pop_back();
        audio->finishPendingReplay();
    }
}

void SDLWindow::scheduleAudioReplay(Audio *audio)
{
    pendingAudioReplays.push_back(audio);
}

void SDLWindow::cancelAudioReplay(Audio *audio)
{
    auto found = std::find(pendingAudioReplays.begin(), pendingAudioReplays.end(), audio);
    if (found != pendingAudioReplays.end())
    {
        *found = pendingAudioReplays.back();
        pendingAudioReplays.pop_back();
    }
}

void SDLWindow::clearTransientAudio()
{
    while (!transientAudioPlaybacks.empty())
    {
        releaseTransientAudioPlayback(transientAudioPlaybacks.size() - 1);
    }
}

void SDLWindow::releaseTransientAudioPlayback(size_t index)
{
    TransientAudioPlayback playback = transientAudioPlaybacks[index];
    size_t lastIndex = transientAudioPlaybacks.size() - 1;
    if (index != lastIndex)
    {
        transientAudioPlaybacks[index] = transientAudioPlaybacks[lastIndex];
    }
    transientAudioPlaybacks.pop_back();

    MIX_DestroyTrack(playback.track);
    playback.audioWrapper->removeReferenceAndFreeIfNoReferences();
}
