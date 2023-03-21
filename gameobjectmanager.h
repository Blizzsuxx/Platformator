#pragma once

#include "gameobject.h"

class GameObjectManager
{
public:
    GameObjectManager();
    ~GameObjectManager();

    void addGameObject(GameObject* gameObject);
    void removeGameObject(GameObject* gameObject);
    void update();
    void render();

private:
    std::list<GameObject*> gameObjects;
};
