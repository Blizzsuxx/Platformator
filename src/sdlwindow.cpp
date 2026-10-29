#include "sdlwindow.h"

#include <algorithm>
#include <iostream>
#include <memory>

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

SDLWindow::SDLWindow(const WindowSettings &windowSettings, const DebugSettings &debugSettings)
    : window(nullptr), renderer(nullptr), mixer(nullptr), mainCamera(nullptr), sdlEvent(),
      debugDraw(DebugDraw::getInstance()), rendererName(nullptr), windowSettings(windowSettings),
      renderWidth(windowSettings.width), renderHeight(windowSettings.height), quit(false),
      paused(debugSettings.startPaused), advanceFrameRequested(false), spriteComponents(), listeners(),
      transientAudioPlaybacks()
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

    // EFFICIENCY: This O(N) pass still inspects every active sprite each rendered frame. A cached visible set could avoid repeated offscreen checks, but it would need generation-safe invalidation for transforms, camera/output changes, activation, and component lifecycle. Keep the authoritative ordered scan here; if the full population matrix shows scan cost dominates, evaluate a generation-safe ordered visibility cache with explicit mutation hooks as follow-up work.
    for (Sprite *spriteComponent : spriteComponents)
    {
        if (!spriteComponent->getGameObject()->getActive())
        {
            continue;
        }

        mainCamera->renderWithView(spriteComponent, renderer, renderView);
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
    spriteComponent->setWindowIndex(SIZE_MAX);
    spriteComponent->setIsRegisteredInWindow(false);
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
