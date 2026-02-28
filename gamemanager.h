#pragma once

#include "sdlwindow.h"
#include "physicsmanager.h"

class GameManager
{
public:
    GameManager();
    ~GameManager();

    GameObject *addGameObject(GameObject *gameObject);
    void removeGameObject(GameObject *gameObject);
    bool removeGameObject(std::string name);

    GameObject *getGameObject(std::string name);

    std::list<GameObject *> &getGameObjects();

    void setPhysicsManager(PhysicsManager *physicsManager);
    PhysicsManager *getPhysicsManager() const;

    SDLWindow *getWindow() const;

    void applyPhysics();
    void resolveCollisions();

    void loop();

private:
    SDLWindow *window;
    std::list<GameObject *> gameObjects;

    PhysicsManager *physicsManager;

    double deltaTime;
    double lastUpdateTime;

    void initializeMainCamera();
    void deleteGameObject(GameObject *gameObject);
    void updateDeltaTime();
    void delay();
};