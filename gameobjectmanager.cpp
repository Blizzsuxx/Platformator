#include "gameobjectmanager.h"

GameObjectManager::GameObjectManager() : gameObjects()
{
}

GameObjectManager::~GameObjectManager()
{
    for (GameObject* gameObject : gameObjects)
    {
        delete gameObject;
    }
}

GameObject* GameObjectManager::addGameObject(GameObject* gameObject)
{
    gameObjects.push_back(gameObject);
    return gameObject;
}

GameObject* GameObjectManager::removeGameObject(GameObject* gameObject)
{
    gameObjects.remove(gameObject);
    delete gameObject;

    return gameObject;
}

GameObject* GameObjectManager::removeGameObject(int index)
{
    std::list<GameObject*>::iterator it = gameObjects.begin();
    std::advance(it, index);
    GameObject* gameObject = *it;
    gameObjects.erase(it);
    delete gameObject;

    return gameObject;
}

GameObject* GameObjectManager::removeGameObject(std::string name)
{
    for (GameObject* gameObject : gameObjects)
    {
        if (gameObject->getName() == name)
        {
            gameObjects.remove(gameObject);
            delete gameObject;
            return gameObject;
        }
    }

    return nullptr;
}

GameObject* GameObjectManager::getGameObject(int index)
{
    std::list<GameObject*>::iterator it = gameObjects.begin();
    std::advance(it, index);
    return *it;
}

GameObject* GameObjectManager::getGameObject(std::string name)
{
    for (GameObject* gameObject : gameObjects)
    {
        if (gameObject->getName() == name)
        {
            return gameObject;
        }
    }

    return nullptr;
}

std::list<GameObject*> GameObjectManager::getGameObjects()
{
    return gameObjects;
}

