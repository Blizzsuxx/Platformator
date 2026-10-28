#include <stdexcept>
#include <cstring>
#include "sdlwindow.h"

#include <algorithm>
#include <iostream>
#include <memory>

#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_version.h>

#include "platformator/audiowrapper.h"
#include "platformator/audio.h"
#include "gamemanager.h"
#include "runtimeaccess.h"
#include "platformator/sprite.h"
#include "benchmark.h"
#include "workprobe.h"

SDLWindow::SDLWindow(const WindowSettings &windowSettings, const DebugSettings &debugSettings,
                     const SpatialSettings &spatialSettings, uint64_t runtimeSerial)
    : window(nullptr), renderer(nullptr), mixer(nullptr), mainCamera(nullptr), sdlEvent(),
      debugDraw(DebugDraw::getInstance()), rendererName(nullptr), windowSettings(windowSettings),
      spatialSettings(spatialSettings), runtimeSerial(runtimeSerial), renderWidth(windowSettings.width),
      renderHeight(windowSettings.height), quit(false), paused(debugSettings.startPaused),
      advanceFrameRequested(false), spriteComponents(), renderCulling(), renderCullingStats(), geometryRevision(0),
      viewRevision(0), drawOrderRevision(0), listeners(), transientAudioPlaybacks()
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
        if (renderer != nullptr && spatialSettings.renderCulling != RenderCullingMode::None)
            renderCulling = std::make_unique<platformator_detail::RenderCulling>(this, spatialSettings, runtimeSerial);

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

    window = SDL_CreateWindow("Platformator", windowSettings.width, windowSettings.height, SDL_WINDOW_RESIZABLE);
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
    renderCulling.reset();

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
    renderCullingStats = {};
    renderCullingStats.geometryRevision = geometryRevision;
    renderCullingStats.viewRevision = viewRevision;
    renderCullingStats.drawOrderRevision = drawOrderRevision;
    if (renderer == nullptr)
    {
        return;
    }
    renderCullingStats.candidateCount = spriteComponents.size();
    PLATFORMATOR_BENCH_BEGIN_RENDER();
    PLATFORMATOR_BENCH_SCOPE(Render);

    SDL_SetRenderDrawColor(renderer, 0xFF, 0xFF, 0xFF, 0xFF);
    SDL_RenderClear(renderer);
    PLATFORMATOR_BENCH_SET_COUNTER(SpriteCount, spriteComponents.size());
    if (!mainCamera)
    {
        renderCullingStats.fallbackReason = "no-camera";
        PLATFORMATOR_BENCH_CANCEL_RENDER();
        SDL_RenderPresent(renderer);
        return;
    }

#if PLATFORMATOR_ENABLE_DEBUG_TOOLS
    PhysicsManager *physicsManager = platformator_detail::RuntimeAccess::gameManager().getPhysicsManager();
    if (shouldSimulateFrame())
    {
        if (const Grid *grid = physicsManager->getGridIfActive())
            for (const auto &cellEntry : grid->getCells())
                debugDraw.addGridCellDebugObject(cellEntry.first);
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

    const std::vector<Sprite *> *submissionOrder = &spriteComponents;
    if (renderCulling)
    {
        PLATFORMATOR_BENCH_SCOPE(RenderCulling);
        submissionOrder = &renderCulling->prepare(mainCamera, renderWidth, renderHeight, renderer, spriteComponents,
                                                  geometryRevision, viewRevision, drawOrderRevision);
        renderCullingStats = renderCulling->getFrameStats();
        renderCullingStats.geometryRevision = geometryRevision;
        renderCullingStats.viewRevision = viewRevision;
        renderCullingStats.drawOrderRevision = drawOrderRevision;
        PLATFORMATOR_BENCH_ADD_SCOPE_DURATION(BoundsPrepare, renderCullingStats.boundsPrepareNanoseconds);
        PLATFORMATOR_BENCH_ADD_SCOPE_DURATION(IndexUpdate, renderCullingStats.indexUpdateNanoseconds);
        PLATFORMATOR_BENCH_ADD_SCOPE_DURATION(IndexMaintenance, renderCullingStats.indexMaintenanceNanoseconds);
        PLATFORMATOR_BENCH_ADD_SCOPE_DURATION(IndexQuery, renderCullingStats.indexQueryNanoseconds);
        PLATFORMATOR_BENCH_ADD_SCOPE_DURATION(ResultReduce, renderCullingStats.resultReduceNanoseconds);
    }
    else
    {
        renderCullingStats.candidateCount = spriteComponents.size();
#if PLATFORMATOR_ENABLE_WORK_PROBES
        platformator_test::work.renderFullOrderWalks += spriteComponents.size();
#endif
    }

    uint64_t outputOrderHash = 1469598103934665603ull;
    {
        PLATFORMATOR_BENCH_SCOPE(RenderSubmit);
        size_t previousIndex = 0;
        bool firstSubmission = true;
        for (Sprite *spriteComponent : *submissionOrder)
        {
            if (!spriteComponent->getGameObject()->getActive())
            {
                continue;
            }

            mainCamera->render(spriteComponent, renderer, renderWidth, renderHeight);
            const uint64_t index = spriteComponent->getWindowIndex();
            if (imageValidationEnabled && !firstSubmission && index <= previousIndex)
                throw std::runtime_error("Image qualification: retained sprite submission order differs from submit-all");
            previousIndex = index;
            firstSubmission = false;
            for (unsigned byte = 0; byte < sizeof(index); ++byte)
            {
                outputOrderHash ^= (index >> (byte * 8)) & 0xffu;
                outputOrderHash *= 1099511628211ull;
            }
            ++renderCullingStats.submittedCount;
            PLATFORMATOR_WORK_RENDER_SUBMISSION(spriteComponent->getWindowIndex());
        }
    }
    renderCullingStats.outputOrderHash = outputOrderHash;
    PLATFORMATOR_BENCH_SET_COUNTER(SpatialProxyCount, renderCullingStats.indexProxyCount);
    PLATFORMATOR_BENCH_SET_COUNTER(ChangedBoundsCount, renderCullingStats.boundsPrepared);
    PLATFORMATOR_BENCH_SET_COUNTER(ProxyInsertCount, renderCullingStats.proxyInserts);
    PLATFORMATOR_BENCH_SET_COUNTER(ProxyRemoveCount, renderCullingStats.proxyRemoves);
    PLATFORMATOR_BENCH_SET_COUNTER(ProxyMoveCount, renderCullingStats.proxyMoves);
    PLATFORMATOR_BENCH_SET_COUNTER(QueryCount, renderCullingStats.visibilityQueries);
    PLATFORMATOR_BENCH_SET_COUNTER(RawDiscoveryCount, renderCullingStats.rawDiscoveries);
    PLATFORMATOR_BENCH_SET_COUNTER(ExactOverlapCount, renderCullingStats.exactChecks);
    PLATFORMATOR_BENCH_SET_COUNTER(RebuildCallCount, renderCullingStats.rebuildCalls);
    PLATFORMATOR_BENCH_SET_COUNTER(RebuildItemCount, renderCullingStats.rebuildItems);
    PLATFORMATOR_BENCH_SET_COUNTER(TreeHeight, renderCullingStats.treeHeight);
    PLATFORMATOR_BENCH_SET_COUNTER(TreeNodeCount, renderCullingStats.treeNodes);
    PLATFORMATOR_BENCH_SET_COUNTER(VisibleCandidateCount, renderCulling ? renderCullingStats.visibleCount : 0);
    PLATFORMATOR_BENCH_SET_COUNTER(SubmittedSpriteCount, renderCullingStats.submittedCount);
    PLATFORMATOR_BENCH_SET_COUNTER(UncullableSpriteCount, renderCullingStats.uncullableCount);
    PLATFORMATOR_BENCH_SET_COUNTER(CullingBypassCount,
                                   renderCullingStats.fallbackReason.empty() ? 0 : 1);
    PLATFORMATOR_BENCH_SET_COUNTER(VisibilityCacheHitCount, renderCullingStats.resultCacheHit ? 1 : 0);
    PLATFORMATOR_BENCH_SET_COUNTER(VisibilityCellCount, renderCullingStats.visibilityCells);

    if (imageValidationEnabled) validateSubmitAllImage();
#if PLATFORMATOR_ENABLE_DEBUG_TOOLS
    debugDraw.render(renderer, mainCamera, renderWidth, renderHeight);
#endif
    SDL_RenderPresent(renderer);
}

void SDLWindow::setImageValidationEnabled(bool enabled)
{
    imageValidationEnabled = enabled;
    imageValidationFrames = 0;
    imageReferenceHash = imageResultHash = 1469598103934665603ull;
}

void SDLWindow::validateSubmitAllImage()
{
    using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
    const auto read = [&]() {
        Surface raw(SDL_RenderReadPixels(renderer, nullptr), SDL_DestroySurface);
        if (!raw) throw std::runtime_error(std::string("Image qualification readback failed: ") + SDL_GetError());
        Surface converted(SDL_ConvertSurface(raw.get(), SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
        if (!converted) throw std::runtime_error("Image qualification pixel conversion failed");
        return converted;
    };
    auto result = read();
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderClear(renderer);
    for (Sprite *sprite : spriteComponents)
        if (sprite->getGameObject()->getActive())
            mainCamera->render(sprite, renderer, renderWidth, renderHeight);
    auto reference = read();
    if (result->w != reference->w || result->h != reference->h)
        throw std::runtime_error("Image qualification output dimensions differ");
    for (int y=0; y<result->h; ++y)
    {
        const auto *actual = static_cast<const unsigned char *>(result->pixels) + y * result->pitch;
        const auto *expected = static_cast<const unsigned char *>(reference->pixels) + y * reference->pitch;
        if (std::memcmp(actual, expected, size_t(result->w)*4) != 0)
            throw std::runtime_error("Image qualification: culled output differs from submit-all pixels");
        // Hash whole words after the exact comparison. SDL_crc32 computes its polynomial
        // bit by bit; it made renderer qualification dominate these validation-only runs.
        const size_t byteCount = size_t(result->w)*4;
        size_t x = 0;
        for (; x + sizeof(uint64_t) <= byteCount; x += sizeof(uint64_t))
        {
            uint64_t actualWord, expectedWord;
            std::memcpy(&actualWord, actual + x, sizeof(uint64_t));
            std::memcpy(&expectedWord, expected + x, sizeof(uint64_t));
            actualWord = SDL_Swap64LE(actualWord);
            expectedWord = SDL_Swap64LE(expectedWord);
            imageResultHash = (imageResultHash ^ actualWord) * 1099511628211ull;
            imageReferenceHash = (imageReferenceHash ^ expectedWord) * 1099511628211ull;
        }
        for (; x < byteCount; ++x)
        {
            imageResultHash = (imageResultHash ^ actual[x]) * 1099511628211ull;
            imageReferenceHash = (imageReferenceHash ^ expected[x]) * 1099511628211ull;
        }
    }
    ++imageValidationFrames;
}

nlohmann::json SDLWindow::getImageValidationProof() const
{
    return {{"status", imageValidationFrames ? "passed" : "not_run"},
            {"method", "same-frame-submit-all-rgba32"}, {"order_status", imageValidationFrames ? "passed" : "not_run"},
            {"frames", imageValidationFrames}, {"tolerance", 0},
            {"reference_hash", std::to_string(imageReferenceHash)}, {"result_hash", std::to_string(imageResultHash)},
            {"hash_algorithm", "fnv64-le-word-rgba32-frame-stream"}, {"renderer_signature", getRendererSignature()}};
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

const platformator_detail::RenderCullingFrameStats &SDLWindow::getRenderCullingStats() const
{
    return renderCullingStats;
}

size_t SDLWindow::getRenderCullingOwnedCapacityBytes() const
{
    return renderCulling ? renderCulling->getOwnedCapacityBytes() : 0;
}

nlohmann::json SDLWindow::getRendererSignature() const
{
    const int version = SDL_GetVersion();
    nlohmann::json signature = {
        {"sdl_version", std::to_string(SDL_VERSIONNUM_MAJOR(version)) + '.' +
                             std::to_string(SDL_VERSIONNUM_MINOR(version)) + '.' +
                             std::to_string(SDL_VERSIONNUM_MICRO(version))},
        {"backend", rendererName ? rendererName : "Unknown"},
        {"output_size", nullptr},
        {"window_pixel_size", nullptr},
        {"target", nullptr},
        {"viewport", nullptr},
        {"renderer_scale", nullptr},
        {"logical_presentation", nullptr},
        {"vsync", nullptr}};
    if (renderer == nullptr)
    {
        signature["status"] = "unavailable";
        signature["reason"] = "renderer-unavailable";
        return signature;
    }
    int width = 0, height = 0;
    if (SDL_GetCurrentRenderOutputSize(renderer, &width, &height))
        signature["output_size"] = {width, height};
    if (window != nullptr && SDL_GetWindowSizeInPixels(window, &width, &height))
        signature["window_pixel_size"] = {width, height};
    SDL_Texture *target = SDL_GetRenderTarget(renderer);
    if (target == nullptr)
        signature["target"] = {{"kind", "window"}};
    else
    {
        float targetWidth = 0, targetHeight = 0;
        SDL_GetTextureSize(target, &targetWidth, &targetHeight);
        signature["target"] = {{"kind", "texture"}, {"size", {targetWidth, targetHeight}}};
    }
    SDL_Rect viewport{};
    if (SDL_GetRenderViewport(renderer, &viewport))
        signature["viewport"] = {viewport.x, viewport.y, viewport.w, viewport.h};
    float scaleX = 0, scaleY = 0;
    if (SDL_GetRenderScale(renderer, &scaleX, &scaleY))
        signature["renderer_scale"] = {scaleX, scaleY};
    int logicalWidth = 0, logicalHeight = 0;
    SDL_RendererLogicalPresentation presentation = SDL_LOGICAL_PRESENTATION_DISABLED;
    if (SDL_GetRenderLogicalPresentation(renderer, &logicalWidth, &logicalHeight, &presentation))
        signature["logical_presentation"] = {logicalWidth, logicalHeight, static_cast<int>(presentation)};
    int vsync = 0;
    if (SDL_GetRenderVSync(renderer, &vsync))
        signature["vsync"] = vsync;
    signature["status"] = "available";
    return signature;
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

    spriteComponent->setWindowIndex(spriteComponents.size());
    spriteComponent->setIsRegisteredInWindow(true);
    spriteComponents.push_back(spriteComponent);
    ++geometryRevision;
    ++drawOrderRevision;
    if (renderCulling)
        renderCulling->addSprite(spriteComponent);
}

void SDLWindow::removeSpriteComponent(Sprite *spriteComponent)
{
    if (spriteComponent == nullptr || !spriteComponent->getIsRegisteredInWindow())
    {
        return;
    }

    size_t removeIndex = spriteComponent->getWindowIndex();
    size_t lastIndex = spriteComponents.size() - 1;

    if (removeIndex != lastIndex)
    {
        Sprite *movedSprite = spriteComponents[lastIndex];
        spriteComponents[removeIndex] = movedSprite;
        movedSprite->setWindowIndex(removeIndex);
    }

    spriteComponents.pop_back();
    if (renderCulling)
        renderCulling->removeSprite(spriteComponent);
    spriteComponent->setWindowIndex(SIZE_MAX);
    spriteComponent->setIsRegisteredInWindow(false);
    ++geometryRevision;
    ++drawOrderRevision;
}

void SDLWindow::setMainCamera(Camera *mainCamera)
{
    if (this->mainCamera == mainCamera)
        return;
    this->mainCamera = mainCamera;
    if (renderCulling)
        renderCulling->setCamera(mainCamera);
    markViewDirty();
}

void SDLWindow::markSpriteDirty(Sprite *sprite)
{
    if (sprite == nullptr || !sprite->getIsRegisteredInWindow())
        return;
    ++geometryRevision;
    if (renderCulling)
        renderCulling->markSpriteDirty(sprite);
}

void SDLWindow::markGameObjectDirty(GameObject *gameObject)
{
    if (!gameObject)
        return;
    if (auto *sprite = gameObject->getComponent<Sprite>())
        markSpriteDirty(sprite);
    if (mainCamera && mainCamera->getGameObject() == gameObject)
        markViewDirty();
}

void SDLWindow::markViewDirty()
{
    ++viewRevision;
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
    const int previousWidth = renderWidth;
    const int previousHeight = renderHeight;
    if (renderer != nullptr && SDL_GetRenderOutputSize(renderer, &renderWidth, &renderHeight))
    {
        renderWidth = std::max(1, renderWidth);
        renderHeight = std::max(1, renderHeight);
        if (renderWidth != previousWidth || renderHeight != previousHeight)
            markViewDirty();
        return;
    }

    if (window != nullptr && SDL_GetWindowSizeInPixels(window, &renderWidth, &renderHeight))
    {
        renderWidth = std::max(1, renderWidth);
        renderHeight = std::max(1, renderHeight);
        if (renderWidth != previousWidth || renderHeight != previousHeight)
            markViewDirty();
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
