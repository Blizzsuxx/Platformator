#pragma once

#include "gameobject.h"

class GameObjectManager
{
public:
    GameObjectManager();
    ~GameObjectManager();

    GameObject* addGameObject(GameObject* gameObject);
    GameObject* removeGameObject(GameObject* gameObject);
    GameObject* removeGameObject(int index);
    GameObject* removeGameObject(std::string name);

    GameObject* getGameObject(int index);
    GameObject* getGameObject(std::string name);

    std::list<GameObject*> getGameObjects();

private:
    std::list<GameObject*> gameObjects;
};
