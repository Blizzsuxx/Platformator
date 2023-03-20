#pragma once

#include "sdlwindow.h"
#include "physicsmanager.h"
#include "gameobjectmanager.h"

class GameManager
{
public:
    GameManager();
    ~GameManager();

    void init();
    void run();
    void shutdown();

private:
    SDLWindow* window;
    PhysicsManager* physicsManager;
    GameObjectManager* gameObjectManager;
};