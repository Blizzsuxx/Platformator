#pragma once

#include "sdlwindow.h"
#include "physicsmanager.h"
#include <vector>
#include "scene.h"

class GameManager
{
    friend class GameObject;
    friend class Component;
    friend class Collider;
    friend class Rigidbody;
    friend class SDLWindow;

public:
    static GameManager &getInstance()
    {
        static GameManager instance;
        return instance;
    }

    GameManager(GameManager &) = delete;
    GameManager &operator=(const GameManager &) = delete;

    GameObject *getGameObject(std::string name);
    GameObject *createGameObject();
    void destroyGameObject(GameObject *gameObject);

    std::list<GameObject *> &getGameObjects();

    SDLWindow *getWindow() const;

    void loop();
    void simulateFrame(double timeDelta);
    TextureWrapper *loadTexture(const std::string &filePath);
    void freeTexture(const std::string &filePath);
    void freeAllTextures();

    void loadScene(Scene &scene);
    void saveScene(const Scene &scene);
    std::vector<Scene> &getScenes();
    void addScene(const Scene &scene);

private:
    GameManager();
    ~GameManager();

    SDLWindow *window;
    std::list<GameObject *> gameObjects;
    std::unordered_map<std::string, TextureWrapper> textureCache;
    std::vector<GameObject *> gameObjectsToDelete;
    std::vector<Scene> scenes;

    PhysicsManager *physicsManager;

    double deltaTime;
    double lastUpdateTime;

    void createMainCameraIfNoMainCameraExists();
    void startDeletingGameObject(GameObject *gameObject);
    void updateDeltaTime();
    void delay();

    GameObject *addGameObject(GameObject *gameObject);
    void removeGameObject(GameObject *gameObject);
    bool removeGameObject(std::string name);

    void notifyPhysicsManagerOfComponentAdded(Component *component);
    void notifyWindowOfComponentAdded(Component *component);

    void notifyPhysicsManagerOfComponentRemoved(Component *component);
    void notifyWindowOfComponentRemoved(Component *component);

    void notifyComponentAdded(Component *component);
    void notifyComponentRemoved(Component *component);
    void deleteMarkedGameObjects();

    void setPhysicsManager(PhysicsManager *physicsManager);
    PhysicsManager *getPhysicsManager() const;
};