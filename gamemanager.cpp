#include "gamemanager.h"

GameManager::GameManager() : window(new SDLWindow()), gameObjectManager(new GameObjectManager()), physicsManager(new PhysicsManager(gameObjectManager))
{
}

GameManager::~GameManager()
{
    delete window;
    delete physicsManager;
    delete gameObjectManager;
}