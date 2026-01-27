#include "gameobjectmanager.h"

GameObjectManager::GameObjectManager()
{
}

GameObjectManager::~GameObjectManager()
{
    for (GameObject *gameObject : gameObjects)
    {
        delete gameObject;
    }
}

GameObject *GameObjectManager::addGameObject(GameObject *gameObject)
{
    gameObjects.push_back(gameObject);

    return gameObject;
}

void GameObjectManager::removeGameObject(GameObject *gameObject)
{
    gameObjects.remove(gameObject);
    delete gameObject;
}

void GameObjectManager::removeGameObject(size_t index)
{
    std::list<GameObject *>::iterator it = gameObjects.begin();
    std::advance(it, index);
    GameObject *gameObject = *it;
    gameObjects.erase(it);
    delete gameObject;
}

bool GameObjectManager::removeGameObject(std::string name)
{
    for (GameObject *gameObject : gameObjects)
    {
        if (gameObject->getName() == name)
        {
            gameObjects.remove(gameObject);
            delete gameObject;
            return true;
        }
    }

    return false;
}

GameObject *GameObjectManager::getGameObject(size_t index)
{
    std::list<GameObject *>::iterator it = gameObjects.begin();
    std::advance(it, index);
    return *it;
}

GameObject *GameObjectManager::getGameObject(std::string name)
{
    for (GameObject *gameObject : gameObjects)
    {
        if (gameObject->getName() == name)
        {
            return gameObject;
        }
    }

    return nullptr;
}

std::list<GameObject *> GameObjectManager::getGameObjects()
{
    return gameObjects;
}

void GameObjectManager::setPhysicsManager(PhysicsManager *physicsManager)
{
    this->physicsManager = physicsManager;
}
