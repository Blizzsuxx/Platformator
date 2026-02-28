#include "sdlwindow.h"
#include <iostream>

SDLWindow::SDLWindow() : window(nullptr), renderer(nullptr),
                         mainCamera(nullptr), sdlEvent(), debugDraw(DebugDraw::getInstance()), quit(false), spriteComponents(), listeners()
{
    if (!init())
    {
        printf("Failed to initialize SDLWindow!");
    }

    listeners.push_back([this](SDL_Event event)
                        {
        if (event.type == SDL_KEYDOWN)
        {
            switch (event.key.keysym.sym)
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

    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0)
    {
        printf("SDL_mixer could not initialize! SDL_mixer Error: %s", Mix_GetError());
        return false;
    }

    if (IMG_Init(IMG_INIT_PNG) != IMG_INIT_PNG)
    {
        printf("SDL_image could not initialize! SDL_image Error: %s", IMG_GetError());
        return false;
    }

    if (TTF_Init() == -1)
    {
        printf("SDL_ttf could not initialize! SDL_ttf Error: %s", TTF_GetError());
        return false;
    }

    // Create window
    window = SDL_CreateWindow("Platformator", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN);
    if (window == nullptr)
    {
        printf("Window could not be created! SDL_Error: %s", SDL_GetError());
        return false;
    }

    // Get window surface
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

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
    IMG_Quit();
    Mix_Quit();
    TTF_Quit();
    SDL_Quit();
}

void SDLWindow::handleEvents()
{
    // Handle events on queue
    while (SDL_PollEvent(&sdlEvent) != 0)
    {
        if (sdlEvent.type == SDL_QUIT)
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

    for (Sprite *spriteComponent : spriteComponents)
    {
        if (spriteComponent->getGameObject()->getActive() == true)
        {
            int renderX = static_cast<int>(spriteComponent->getGameObject()->getPosition().x() - mainCamera->getCamera().x);
            int renderY = static_cast<int>(spriteComponent->getGameObject()->getPosition().y() - mainCamera->getCamera().y);

            int renderW = static_cast<int>(spriteComponent->getWidth() * spriteComponent->getGameObject()->getScale().x());
            int renderH = static_cast<int>(spriteComponent->getHeight() * spriteComponent->getGameObject()->getScale().y());

            SDL_Rect renderQuad = {renderX, renderY, renderW, renderH};
            SDL_RenderCopyEx(renderer, spriteComponent->getTexture(), nullptr, &renderQuad, spriteComponent->getGameObject()->getRotationInDegrees(), nullptr, spriteComponent->getFlip());

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
    spriteComponents.push_back(spriteComponent);
}

void SDLWindow::removeSpriteComponent(Sprite *spriteComponent)
{
    spriteComponents.remove(spriteComponent);
}

void SDLWindow::setMainCamera(Camera *mainCamera)
{
    this->mainCamera = mainCamera;
}