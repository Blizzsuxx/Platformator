#include "sdlwindow.h"
#include "gamemanager.h"
#include <iostream>

SDLWindow::SDLWindow() : window(nullptr), renderer(nullptr),
                         mixer(nullptr), mainCamera(nullptr), sdlEvent(), debugDraw(DebugDraw::getInstance()), rendererName(nullptr), quit(false), frameAdvanceMode(false), advanceFrameRequested(false), spriteComponents(), listeners()
{
    if (!init())
    {
        printf("Failed to initialize SDLWindow!");
        quit = true;
        return;
    }

    listeners.push_back([this](SDL_Event event, double deltaTime)
                        {
        if (event.type == SDL_EVENT_KEY_DOWN)
        {
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
            }
        } });
}

SDLWindow::~SDLWindow()
{
    close();
}

bool SDLWindow::init()
{
    // Initialize SDL
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

    // if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG))
    // {
    //     printf("SDL_image could not initialize! SDL_image Error: %s", IMG_GetError());
    //     return false;
    // }

    if (TTF_Init() == -1)
    {
        printf("SDL_ttf could not initialize! SDL_ttf Error: %s", SDL_GetError());
        close();
        return false;
    }

    // Create window
    window = SDL_CreateWindow("Platformator", SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_MAXIMIZED);
    if (window == nullptr)
    {
        printf("Window could not be created! SDL_Error: %s", SDL_GetError());
        close();
        return false;
    }

    // Get window surface
    renderer = SDL_CreateRenderer(window, nullptr);

    if (renderer == nullptr)
    {
        printf("Renderer could not be created! SDL_Error: %s", SDL_GetError());
        close();
        return false;
    }

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

    // Destroy window
    mainCamera = nullptr;
    rendererName = nullptr;

    // Quit SDL subsystems
    // IMG_Quit();
    MIX_Quit();
    TTF_Quit();
    SDL_Quit();
}

void SDLWindow::handleSDLEvents(double deltaTime)
{
    // Handle events on queue
    while (SDL_PollEvent(&sdlEvent) != 0)
    {
        if (sdlEvent.type == SDL_EVENT_QUIT)
        {
            quit = true;
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
        if (spriteComponent->getGameObject()->getActive() == true)
        {
            mainCamera->render(spriteComponent, renderer);

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
    }

    debugDraw.render(renderer, mainCamera);

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
    bool value = !frameAdvanceMode || advanceFrameRequested;
    return value;
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