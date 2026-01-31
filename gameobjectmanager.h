#pragma once

#include "gameobject.h"
#include "physicsmanager.h"

class GameObjectManager
{
public:
    GameObjectManager();
    ~GameObjectManager();

    GameObject *addGameObject(GameObject *gameObject);
    void removeGameObject(GameObject *gameObject);
    void removeGameObject(size_t index);
    bool removeGameObject(std::string name);

    GameObject *getGameObject(size_t index);
    GameObject *getGameObject(std::string name);

    std::list<GameObject *> getGameObjects();

    void setPhysicsManager(PhysicsManager *physicsManager);
    PhysicsManager *getPhysicsManager() const;

    void applyPhysics();
    void resolveCollisions();

private:
    std::list<GameObject *> gameObjects;

    PhysicsManager *physicsManager;
};