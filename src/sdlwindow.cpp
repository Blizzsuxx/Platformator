#include "sdlwindow.h"

#include <algorithm>
#include <iostream>

#include <SDL3/SDL_properties.h>

#include "audiowrapper.h"
#include "gamemanager.h"

SDLWindow::SDLWindow(const WindowSettings &windowSettings)
    : window(nullptr),
      renderer(nullptr),
      mixer(nullptr),
      mainCamera(nullptr),
      sdlEvent(),
      debugDraw(DebugDraw::getInstance()),
      rendererName(nullptr),
      windowSettings(windowSettings),
      renderWidth(windowSettings.width),
      renderHeight(windowSettings.height),
      quit(false),
      frameAdvanceMode(false),
      advanceFrameRequested(false),
      spriteComponents(),
      listeners(),
      transientAudioPlaybacks()
{
    if (!init())
    {
        printf("Failed to initialize SDLWindow!");
        quit = true;
        return;
    }

    listeners.push_back([this](SDL_Event event, double)
                        {
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
            frameAdvanceMode = !frameAdvanceMode;
            advanceFrameRequested = false;
            printf("Frame advance mode: %s\n", frameAdvanceMode ? "enabled" : "disabled");
            break;
        case SDLK_F6:
            if (frameAdvanceMode)
            {
                advanceFrameRequested = true;
                printf("Advancing one frame\n");
            }
            break;
        default:
            break;
        } });
}

SDLWindow::~SDLWindow()
{
    close();
}

bool SDLWindow::init()
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0)
    {
        printf("SDL could not initialize! SDL_Error: %s", SDL_GetError());
        return false;
    }

    if (!MIX_Init())
    {
        printf("SDL_mixer could not initialize! SDL_mixer Error: %s", SDL_GetError());
        close();
        return false;
    }

    mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (mixer == nullptr)
    {
        printf("SDL_mixer could not create a mixer device! SDL_mixer Error: %s", SDL_GetError());
        close();
        return false;
    }

    if (TTF_Init() == -1)
    {
        printf("SDL_ttf could not initialize! SDL_ttf Error: %s", SDL_GetError());
        close();
        return false;
    }

    window = SDL_CreateWindow("Platformator", windowSettings.width, windowSettings.height, SDL_WINDOW_RESIZABLE);
    if (window == nullptr)
    {
        printf("Window could not be created! SDL_Error: %s", SDL_GetError());
        close();
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
        close();
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

    printf("Renderer Used: %s\n", rendererName);
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
    TTF_Quit();
    SDL_Quit();
}

void SDLWindow::handleSDLEvents(double deltaTime)
{
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
    if (renderer == nullptr || mainCamera == nullptr)
    {
        return;
    }

    SDL_SetRenderDrawColor(renderer, 0xFF, 0xFF, 0xFF, 0xFF);
    SDL_RenderClear(renderer);

    PhysicsManager *physicsManager = GameManager::getInstance().getPhysicsManager();
    if (shouldSimulateFrame())
    {
        for (const auto &cellEntry : physicsManager->getGrid().getCells())
        {
            debugDraw.addGridCellDebugObject(cellEntry.first);
        }
    }

    for (Sprite *spriteComponent : spriteComponents)
    {
        if (!spriteComponent->getGameObject()->getActive())
        {
            continue;
        }

        mainCamera->render(spriteComponent, renderer, renderWidth, renderHeight);

        Collider *collider = (Collider *)spriteComponent->getGameObject()->getComponent(ComponentType::COLLIDER);
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

    debugDraw.render(renderer, mainCamera, renderWidth, renderHeight);
    SDL_RenderPresent(renderer);
}

void SDLWindow::clearDebugObjects()
{
    debugDraw.clearDebugObjects();
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

bool SDLWindow::isRunning() const
{
    return !quit;
}

bool SDLWindow::getIsFrameAdvanceMode() const
{
    return frameAdvanceMode;
}

bool SDLWindow::shouldSimulateFrame() const
{
    return !frameAdvanceMode || advanceFrameRequested;
}

void SDLWindow::clearAdvanceFrameRequest()
{
    advanceFrameRequested = false;
}

void SDLWindow::addSpriteComponent(Sprite *spriteComponent)
{
    if (spriteComponent == nullptr || spriteComponent->getIsRegisteredInWindow() || !spriteComponent->getGameObject()->getActive())
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
    MIX_Track *track = MIX_CreateTrack(mixer);

    if (!MIX_SetTrackAudio(track, audioWrapper->getAudio()))
    {
        MIX_DestroyTrack(track);
        return false;
    }

    MIX_SetTrackGain(track, std::max(0.0f, gain));

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

            MIX_DestroyTrack(track);
            return false;
        }
    }

    const bool played = MIX_PlayTrack(track, options);
    if (options != 0)
    {
        SDL_DestroyProperties(options);
    }

    if (!played)
    {
        MIX_DestroyTrack(track);
        return false;
    }

    audioWrapper->addReference();
    transientAudioPlaybacks.push_back({track, audioWrapper});
    return true;
}

void SDLWindow::updateTransientAudio()
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