#pragma once

#include "sdlwindow.h"
#include "physicsmanager.h"
#include <vector>
#include "scene.h"
#include "audiowrapper.h"

class Animator;

class GameManager
{
    friend class GameObject;
    friend class Component;
    friend class Collider;
    friend class Rigidbody;
    friend class SDLWindow;
    friend class ColliderPair;

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

    std::vector<GameObject *> &getGameObjects();

    SDLWindow *getWindow() const;

    void loop();
    void simulateFrame(double timeDelta);
    TextureWrapper *loadTexture(const std::string &filePath);
    void freeTexture(TextureWrapper *textureWrapper);
    void freeAllTextures();
    AudioWrapper *loadAudio(const std::string &filePath);
    void freeAudio(AudioWrapper *audioWrapper);
    void freeAllAudio();

    void loadScene(Scene &scene);
    void saveScene(const Scene &scene);
    std::vector<Scene> &getScenes();
    void addScene(const Scene &scene);

    void addUserScriptListeners(const std::function<void(double)> &event);

private:
    GameManager();
    ~GameManager();

    SDLWindow *window;
    std::vector<GameObject *> gameObjects;
    std::vector<Animator *> animatorComponents;
    std::unordered_map<std::string, TextureWrapper> textureCache;
    std::unordered_map<std::string, AudioWrapper> audioCache;
    std::vector<GameObject *> gameObjectsToDelete;
    std::vector<Scene> scenes;
    std::vector<std::function<void(double)>> userScriptListeners;

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

    void notifyRuntimeOfComponentAdded(Component *component);
    void notifyRuntimeOfComponentRemoved(Component *component);
    void notifyComponentAdded(Component *component);
    void notifyComponentRemoved(Component *component);
    void deleteMarkedGameObjects();

    void setPhysicsManager(PhysicsManager *physicsManager);
    PhysicsManager *getPhysicsManager() const;
    void handleUserScriptListeners(double timeDelta);
    void updateAnimatorComponents(double timeDelta);
};