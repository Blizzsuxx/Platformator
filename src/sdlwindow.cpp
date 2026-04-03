#include "sdlwindow.h"
#include "gamemanager.h"
#include <iostream>

SDLWindow::SDLWindow() : window(nullptr), renderer(nullptr),
                         mainCamera(nullptr), sdlEvent(), debugDraw(DebugDraw::getInstance()), rendererName(nullptr), quit(false), spriteComponents(), listeners()
{
    if (!init())
    {
        printf("Failed to initialize SDLWindow!");
    }

    listeners.push_back([this](SDL_Event event)
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
        return false;
    }

    // Create window
    window = SDL_CreateWindow("Platformator", SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_MAXIMIZED);
    if (window == nullptr)
    {
        printf("Window could not be created! SDL_Error: %s", SDL_GetError());
        return false;
    }

    // Get window surface
    renderer = SDL_CreateRenderer(window, nullptr);

    rendererName = SDL_GetRendererName(renderer);
    printf("Renderer Used: %s\n", rendererName);

    SDL_SetWindowTitle(window, ("Platformator: " + std::string(rendererName)).c_str());

    return true;
}

void SDLWindow::close()
{
    // Destroy window
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    window = nullptr;
    renderer = nullptr;
    mainCamera = nullptr;

    // Quit SDL subsystems
    // IMG_Quit();
    MIX_Quit();
    TTF_Quit();
    SDL_Quit();
}

void SDLWindow::handleEvents()
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
            listener(sdlEvent);
        }
    }
}

void SDLWindow::render()
{
    SDL_SetRenderDrawColor(renderer, 0xFF, 0xFF, 0xFF, 0xFF);
    SDL_RenderClear(renderer);

    PhysicsManager *physicsManager = GameManager::getInstance().getPhysicsManager();
    if (physicsManager != nullptr)
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
            float renderX = spriteComponent->getGameObject()->getPosition().x() - mainCamera->getCamera().x;
            float renderY = spriteComponent->getGameObject()->getPosition().y() - mainCamera->getCamera().y;

            float renderW = spriteComponent->getWidth() * spriteComponent->getGameObject()->getScale().x();
            float renderH = spriteComponent->getHeight() * spriteComponent->getGameObject()->getScale().y();

            renderX -= renderW / 2;
            renderY -= renderH / 2;

            SDL_FRect renderQuad = {renderX, renderY, renderW, renderH};
            SDL_RenderTextureRotated(renderer, spriteComponent->getTexture(), nullptr, &renderQuad, spriteComponent->getGameObject()->getRotationInDegrees(), nullptr, spriteComponent->getFlip());

            Collider *collider = (Collider *)spriteComponent->getGameObject()->getComponent(ComponentType::COLLIDER);
            if (collider)
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

SDL_Window *SDLWindow::getWindow() const
{
    return window;
}

SDL_Renderer *SDLWindow::getRenderer() const
{
    return renderer;
}

bool SDLWindow::isRunning() const
{
    return !quit;
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

void SDLWindow::addSdlListener(const std::function<void(SDL_Event)> &listener)
{
    listeners.push_back(listener);
}