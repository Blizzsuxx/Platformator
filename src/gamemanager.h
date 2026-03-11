#pragma once

#include "sdlwindow.h"
#include "physicsmanager.h"

class GameManager
{
public:
    static GameManager &getInstance()
    {
        static GameManager instance;
        return instance;
    }

    GameManager(GameManager &) = delete;
    GameManager &operator=(const GameManager &) = delete;

    GameObject *addGameObject(GameObject *gameObject);
    void removeGameObject(GameObject *gameObject);
    bool removeGameObject(std::string name);

    GameObject *getGameObject(std::string name);

    std::list<GameObject *> &getGameObjects();

    void setPhysicsManager(PhysicsManager *physicsManager);
    PhysicsManager *getPhysicsManager() const;

    SDLWindow *getWindow() const;

    void loop();
    TextureWrapper *loadTexture(const std::string &filePath);
    void freeTexture(const std::string &filePath);
    void freeAllTextures();

private:
    GameManager();
    ~GameManager();

    SDLWindow *window;
    std::list<GameObject *> gameObjects;
    std::unordered_map<std::string, TextureWrapper> textureCache;

    PhysicsManager *physicsManager;

    double deltaTime;
    double lastUpdateTime;

    void initializeMainCamera();
    void deleteGameObject(GameObject *gameObject);
    void updateDeltaTime();
    void delay();
};