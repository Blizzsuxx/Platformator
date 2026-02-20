#include "gamemanager.h"

GameManager::GameManager() : window(new SDLWindow()), gameObjectManager(new GameObjectManager())
{
}

GameManager::~GameManager()
{
    delete window;
    delete gameObjectManager;
}

void GameManager::loop()
{
    while (window->isRunning())
    {
        window->handleEvents();
        gameObjectManager->applyPhysics();
        gameObjectManager->resolveCollisions();
        window->render();
    }
}