#pragma once

#include "gameobject.h"

class PhysicsManager;

class GameObjectManager
{
public:
    GameObjectManager();
    ~GameObjectManager();

    GameObject* addGameObject(GameObject* gameObject);
    void removeGameObject(GameObject* gameObject);
    void removeGameObject(int index);
    bool removeGameObject(std::string name);

    GameObject* getGameObject(int index);
    GameObject* getGameObject(std::string name);

    std::list<GameObject*> getGameObjects();

    void setPhysicsManager(PhysicsManager* physicsManager);

private:
    std::list<GameObject*> gameObjects;

    PhysicsManager* physicsManager;
};


class PhysicsManager
{
public:
    PhysicsManager(GameObjectManager* gameObjectManager);
    ~PhysicsManager();

    void applyPhysics();
    void resolveCollisions();

private:
    GameObjectManager* gameObjectManager;

    std::list<Component*> RigidBodyComponents;
    std::list<Component*> ColliderComponents;
};