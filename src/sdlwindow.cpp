#include "sdlwindow.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

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
platformator_detail::RenderCullingMode requestedRenderCullingModeFromEnvironment()
{
    // This process-local control is deliberately internal. It is read while
    // SDLWindow is constructed, before Runtime publishes scene components.
    const char *value = std::getenv("PLATFORMATOR_RENDER_CULLING_MODE");
    if (value == nullptr || *value == '\0' || std::string_view(value) == "indexed")
        return platformator_detail::RenderCullingMode::Indexed;
    if (std::string_view(value) == "current-linear")
        return platformator_detail::RenderCullingMode::CurrentLinear;
    if (std::string_view(value) == "cached-linear")
        return platformator_detail::RenderCullingMode::CachedLinear;
    throw std::invalid_argument("Unknown PLATFORMATOR_RENDER_CULLING_MODE: " + std::string(value));
}

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

SDLWindow::SDLWindow(const WindowSettings &windowSettings, const DebugSettings &debugSettings)
    : window(nullptr), renderer(nullptr), mixer(nullptr), mainCamera(nullptr), sdlEvent(),
      debugDraw(DebugDraw::getInstance()), rendererName(nullptr), windowSettings(windowSettings),
      renderWidth(windowSettings.width), renderHeight(windowSettings.height), quit(false),
      paused(debugSettings.startPaused), advanceFrameRequested(false),
      requestedRenderCullingMode(requestedRenderCullingModeFromEnvironment()),
      effectiveRenderCullingMode(windowSettings.enableRenderCulling
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

        if (renderer != nullptr && effectiveRenderCullingMode != platformator_detail::RenderCullingMode::SubmitAll &&
            effectiveRenderCullingMode != platformator_detail::RenderCullingMode::CurrentLinear)
        {
            renderCullingState = std::make_unique<platformator_detail::RenderCullingState>(
                effectiveRenderCullingMode);
            renderCullingState->publishCapacityObservation();
            PLATFORMATOR_RENDER_CULLING_ADD(renderCullingStateCreations, 1);
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
                paused = !paused;
                advanceFrameRequested = false;
                PLATFORMATOR_LOG("Simulation: %s\n", paused ? "paused" : "running");
                break;
            case SDLK_F6:
                if (paused)
                {
                    advanceFrameRequested = true;
                    PLATFORMATOR_LOG("Advancing one frame\n");
                }
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

    if (!TTF_Init())
    {
        printf("SDL_ttf could not initialize! SDL_ttf Error: %s", SDL_GetError());
        return false;
    }

    SDL_WindowFlags windowFlags = SDL_WINDOW_RESIZABLE;
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
    if (TTF_WasInit() != 0)
    {
        TTF_Quit();
    }
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

    SDL_SetRenderDrawColor(renderer, 0xFF, 0xFF, 0xFF, 0xFF);
    SDL_RenderClear(renderer);
    if (!mainCamera)
    {
        PLATFORMATOR_RENDER_BEFORE_PRESENT(renderer, renderWidth, renderHeight);
        SDL_RenderPresent(renderer);
        return;
    }

#if PLATFORMATOR_ENABLE_DEBUG_TOOLS
    PhysicsManager *physicsManager = platformator_detail::RuntimeAccess::gameManager().getPhysicsManager();
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

    if (renderCullingState && prepareCachedLinear(mainCamera, renderView))
    {
        for (const auto rank : renderCullingState->visibleRanks())
            mainCamera->renderWithView(spriteComponents[rank], renderer, renderView, false);
    }
    else
    {
        for (Sprite *spriteComponent : spriteComponents)
        {
            if (!spriteComponent->getGameObject()->getActive())
                continue;
            mainCamera->renderWithView(spriteComponent, renderer, renderView);
        }
    }

#if PLATFORMATOR_ENABLE_DEBUG_TOOLS
    debugDraw.render(renderer, mainCamera, renderWidth, renderHeight);
#endif
    PLATFORMATOR_RENDER_BEFORE_PRESENT(renderer, renderWidth, renderHeight);
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

bool SDLWindow::prepareCachedLinear(Camera *camera, const platformator_detail::SpriteRenderView &view)
{
    using namespace platformator_detail;
    if (!renderCullingState || camera == nullptr)
        return false;
    if (!view.cullingEligible)
    {
        PLATFORMATOR_RENDER_CULLING_ADD(renderCullingMappingFallbacks, 1);
        return false;
    }

    const SpriteRenderProjectionKey projection = makeSpriteRenderProjectionKey(view);
    const SpriteRenderViewKey currentView = makeSpriteRenderViewKey(camera->getHandle(), view);
    SpriteRenderBounds query;
    if (!projection.valid || !currentView.valid || !makeCertifiedRenderViewQuery(view, query))
    {
        PLATFORMATOR_RENDER_CULLING_ADD(renderCullingNumericFallbacks, 1);
        return false;
    }

    auto &state = *renderCullingState;
    const bool projectionChanged = !sameSpriteRenderProjection(state.projectionKey(), projection);
    const bool viewChanged = !sameSpriteRenderView(state.viewKey(), currentView);
    const bool rebuildSelection = projectionChanged || viewChanged || !state.selectionValid();
    if (projectionChanged)
    {
        PLATFORMATOR_RENDER_CULLING_ADD(renderCullingProjectionRebuilds, 1);
        if (state.mode() == RenderCullingMode::Indexed)
            state.setSpatialCellSize(static_cast<double>(view.outputWidth) / view.scaleX,
                                     static_cast<double>(view.outputHeight) / view.scaleY);
    }

    GameManager &manager = RuntimeAccess::gameManager();
    const auto refreshRecord = [&](RenderCullingState::RecordIndex index) {
        auto &record = state.recordAt(index);
        auto *sprite = static_cast<Sprite *>(manager.resolveHandle(record.spriteHandle, ObjectKind::Sprite));
        if (sprite == nullptr)
        {
            record.boundsValid = false;
            record.hasTexture = false;
            if (state.mode() == RenderCullingMode::Indexed)
                state.updateSpatialBounds(index, record.bounds, false);
            PLATFORMATOR_RENDER_CULLING_ADD(renderCullingBoundsRefreshes, 1);
            return;
        }

        const Eigen::Vector2f &position = camera->getPublishedSpritePosition(sprite);
        record.hasTexture = sprite->getTexture() != nullptr;
        record.boundsValid = makeCertifiedSpriteRenderProxy(position.x(), position.y(), sprite->getWidth(),
                                                            sprite->getHeight(),
                                                            sprite->getGameObject()->getRotationInDegrees(),
                                                            view.scaleX, view.scaleY, record.bounds);
        if (state.mode() == RenderCullingMode::Indexed)
            state.updateSpatialBounds(index, record.bounds, record.boundsValid && record.hasTexture);
        PLATFORMATOR_RENDER_CULLING_ADD(renderCullingBoundsRefreshes, 1);
    };

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

    const auto visibleAtCurrentView = [&](RenderCullingState::RecordIndex rank) {
        const auto &record = state.recordAt(rank);
        PLATFORMATOR_RENDER_CULLING_ADD(renderCullingCandidateVisits, 1);
        if (!record.hasTexture || !record.boundsValid)
            return true;
        if (!overlaps(record.bounds, query))
            return false;

        auto *sprite = static_cast<Sprite *>(manager.resolveHandle(record.spriteHandle, ObjectKind::Sprite));
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
        state.beginVisibleSelection();
        if (state.mode() == RenderCullingMode::Indexed)
        {
            const auto &candidates = state.querySpatial(query);
            for (const auto rank : candidates)
                if (visibleAtCurrentView(rank))
                    state.appendVisibleRank(rank);
            state.sortVisibleRanks();
        }
        else
        {
            for (RenderCullingState::RecordIndex rank = 0; rank < state.recordCount(); ++rank)
            {
                PLATFORMATOR_RENDER_CULLING_ADD(renderCullingFullPopulationVisits, 1);
                if (visibleAtCurrentView(rank))
                    state.appendVisibleRank(rank);
            }
        }
    }
    else if (state.dirtyCount() == 0)
    {
        PLATFORMATOR_RENDER_CULLING_ADD(renderCullingCacheHits, 1);
    }
    else
    {
        const auto queuedCount = state.dirtyCount();
        state.beginVisibleRankChanges();
        for (RenderCullingState::RecordIndex queued = 0; queued < queuedCount; ++queued)
        {
            const auto rank = state.dirtyAt(queued);
            state.queueVisibleRankChange(rank, visibleAtCurrentView(rank));
        }
        state.applyVisibleRankChanges();
    }

    state.setPreparedKeys(projection, currentView);
    state.clearDirtyQueue();
    return true;
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
    return false;
#endif
}

bool SDLWindow::shouldSimulateFrame() const
{
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
#endif
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
        spriteComponents.reserve(std::max(required, std::max<size_t>(4, doubled)));
    }
    if (renderCullingState)
        renderCullingState->reserveRecords(required);
    if (renderCullingState)
        renderCullingState->appendRecord(spriteComponent->getHandle());
    spriteComponents.push_back(spriteComponent);
    spriteComponent->setWindowIndex(newIndex);
    spriteComponent->setIsRegisteredInWindow(true);
}

void SDLWindow::removeSpriteComponent(Sprite *spriteComponent)
{
    if (spriteComponent == nullptr || !spriteComponent->getIsRegisteredInWindow())
    {
        return;
    }

    size_t removeIndex = spriteComponent->getWindowIndex();
    size_t lastIndex = spriteComponents.size() - 1;

    if (renderCullingState)
        renderCullingState->removeRecord(removeIndex);

    if (removeIndex != lastIndex)
    {
        Sprite *movedSprite = spriteComponents[lastIndex];
        spriteComponents[removeIndex] = movedSprite;
        movedSprite->setWindowIndex(removeIndex);
    }

    spriteComponents.pop_back();
    spriteComponent->setWindowIndex(SIZE_MAX);
    spriteComponent->setIsRegisteredInWindow(false);
}

void SDLWindow::notifySpriteGeometryChanged(Sprite *spriteComponent) noexcept
{
    if (!renderCullingState || spriteComponent == nullptr || !spriteComponent->getIsRegisteredInWindow())
        return;

    renderCullingState->markDirty(spriteComponent->getWindowIndex(), spriteComponent->getHandle());
}

void SDLWindow::releaseRenderCullingStorage() noexcept
{
    if (renderCullingState)
        renderCullingState->releaseStorage();
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
        if (audio->isPlaying() || audio->isPaused())
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
