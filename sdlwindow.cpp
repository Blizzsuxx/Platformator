#include "sdlwindow.h"
#include <iostream>

SDLWindow::SDLWindow() : window(nullptr), renderer(nullptr),
                         gameObjectManager(new GameObjectManager()), mainCamera(new Camera(nullptr, SCREEN_WIDTH, SCREEN_HEIGHT)), e(), quit(false)
{
    if (!init())
    {
        printf("Failed to initialize SDLWindow!");
    }
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
    delete gameObjectManager;
    delete mainCamera;

    // Quit SDL subsystems
    IMG_Quit();
    Mix_Quit();
    TTF_Quit();
    SDL_Quit();
}

void SDLWindow::handleEvents()
{
    // Handle events on queue
    while (SDL_PollEvent(&e) != 0)
    {
        // User requests quit
        std::cout << "Event type: " << e.type << std::endl;
        std::cout << "Event key: " << e.key.keysym.sym << std::endl;
        if (e.type == SDL_QUIT)
        {
            std::cout << "Quit event" << std::endl;
            quit = true;
        }
    }
}

void SDLWindow::applyPhysics()
{
}

void SDLWindow::resolveCollisions()
{
}

void SDLWindow::render()
{
    // Clear screen
    SDL_SetRenderDrawColor(renderer, 0xFF, 0xFF, 0xFF, 0xFF);
    SDL_RenderClear(renderer);

    // Update screen
    SDL_RenderPresent(renderer);
}

void SDLWindow::loop()
{
    while (!quit)
    {
        handleEvents();
        applyPhysics();
        resolveCollisions();
        render();
    }
    close();
}

SDL_Window *SDLWindow::getWindow() const
{
    return window;
}

SDL_Renderer *SDLWindow::getRenderer() const
{
    return renderer;
}
