#include "gamemanager.h"

GameManager::GameManager() : window(new SDLWindow()), physicsManager(new PhysicsManager()), gameObjectManager(new GameObjectManager())
{
}

GameManager::~GameManager()
{
    delete window;
    delete physicsManager;
    delete gameObjectManager;
}