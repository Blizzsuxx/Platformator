#pragma once

#include "debugsettings.h"
#include "sdlwindow.h"
#include "physicsmanager.h"
#include "windowsettings.h"
#include <cstdint>
#include <vector>
#include "scene.h"
#include "audiowrapper.h"

class Animator;
class ScriptComponent;
class Behavior;
class AnimationClip;

namespace platformator
{
    class Runtime;
}

class GameManager
{
    friend class platformator::Runtime;
    friend class GameObject;
    friend class Component;
    friend class Collider;
    friend class Rigidbody;
    friend class SDLWindow;
    friend class ColliderPair;
    friend class ScriptComponent;

public:
    static GameManager &getInstance();

    GameManager(GameManager &) = delete;
    GameManager &operator=(const GameManager &) = delete;

    GameManager(const WindowSettings &windowSettings, const DebugSettings &debugSettings);
    ~GameManager();

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
    AnimationClip *loadAnimationClip(const std::string &filePath);
    void freeAnimationClip(AnimationClip *animationClip);
    void freeAllAnimationClips();

    void loadScene(Scene &scene);
    void saveScene(const Scene &scene);

    BaseObject *getObjectById(int id) const;
    const Eigen::Vector2f &getGravityVector() const;
    const Eigen::Vector2f &getGravityVectorNormalized() const;

private:
    static void setCurrentInstance(GameManager *gameManager);
    static GameManager *tryGetCurrentInstance();
    static GameManager *&currentInstance();

    SDLWindow *window;
    std::vector<GameObject *> gameObjects;
    std::vector<Animator *> animatorComponents;
    std::vector<ScriptComponent *> scriptComponents;
    std::unordered_map<std::string, TextureWrapper> textureCache;
    std::unordered_map<std::string, AudioWrapper> audioCache;
    std::unordered_map<std::string, AnimationClip> animationClipCache;
    std::unordered_map<int, BaseObject *> idToObjectMap;
    std::vector<GameObject *> gameObjectsToDelete;
    std::vector<Behavior *> startedBehaviors;

    PhysicsManager *physicsManager;

    double deltaTime;
    double lastUpdateTime;

    void createMainCameraIfNoMainCameraExists();
    void addStartedBehaviorsRecursive(GameObject *gameObject);
    void registerGameObjectSubtree(GameObject *gameObject);
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
    void fixedUpdateScriptComponents(double timeDelta);
    void updateScriptComponents(double timeDelta);
    void lateUpdateScriptComponents(double timeDelta);
    void updateAnimatorComponents(double timeDelta);
    void triggerStartedBehaviors();

    void addStartedBehavior(Behavior *behavior);
    void removeStartedBehavior(Behavior *behavior);
};