#pragma once

#include "sdlwindow.h"
#include "physicsmanager.h"
#include "gameobjectmanager.h"

class GameManager
{
public:
    GameManager();
    ~GameManager();

private:
    SDLWindow* window;
    PhysicsManager* physicsManager;
    GameObjectManager* gameObjectManager;
};