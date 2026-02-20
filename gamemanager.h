#pragma once

#include "sdlwindow.h"
#include "physicsmanager.h"
#include "gameobjectmanager.h"

class GameManager
{
public:
    GameManager();
    ~GameManager();

    void loop();

private:
    SDLWindow *window;
    GameObjectManager *gameObjectManager;
};