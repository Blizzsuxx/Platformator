#pragma once

#include "physicsmanager.h"
#include "platformator/debugsettings.h"
#include "platformator/windowsettings.h"
#include "sdlwindow.h"

#include "objectregistry.h"
#include "assetrequestaliases.h"
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "platformator/animationclip.h"
#include "platformator/audiowrapper.h"
#include "platformator/scene.h"
#include "platformator/texturewrapper.h"

class Animator;
class ScriptComponent;
class Behavior;

namespace platformator
{
class Runtime;
class ReferenceResolver;
} // namespace platformator

namespace platformator_detail
{
class RuntimeAccess;
}

class GameManager
{
    friend class SceneBuildContext;
    friend class platformator::Runtime;
    friend class platformator_detail::RuntimeAccess;
    friend class GameObject;
    friend class Component;
    friend class Collider;
    friend class Rigidbody;
    friend class SDLWindow;
    friend class ColliderPair;
    friend class ScriptComponent;
    friend class PhysicsManager;

  public:
    enum class Phase
    {
        Idle,
        Starting,
        StepHook,
        FixedUpdate,
        Physics,
        CollisionDispatch,
        Update,
        Animation,
        LateUpdate,
        MutationCommit,
        DestructionCallbacks,
        Rendering,
        Saving,
        Shutdown
    };

    class PhaseGuard
    {
      public:
        PhaseGuard(GameManager &manager, Phase next) : manager(manager), previous(manager.phase)
        {
            manager.phase = next;
        }

        ~PhaseGuard()
        {
            manager.phase = previous;
        }

      private:
        GameManager &manager;
        Phase previous;
    };

    void assertCanMutate() const;
    void commitMutations();
    void flushIfIdle();
    void shutdown();
    void clearObjects();
    void requestScene(std::optional<std::string> path);

    bool isIdle() const
    {
        return phase == Phase::Idle;
    }

    BaseObject *resolveHandle(platformator::ObjectHandle handle, platformator_detail::ObjectKind kind) const;

    GameManager(GameManager &) = delete;
    GameManager &operator=(const GameManager &) = delete;

    GameManager(const WindowSettings &windowSettings, const DebugSettings &debugSettings);
    ~GameManager();

    GameObject *getGameObject(std::string name);
    GameObject *createGameObject();
    void destroyGameObject(GameObject *gameObject);

    SDLWindow *getWindow() const;

    void loop();
    void simulateFrame(double timeDelta);
    bool simulateAndRenderFrame(double timeDelta);
    TextureWrapper *loadTexture(const std::string &filePath, bool required = false);
    void freeTexture(TextureWrapper *textureWrapper);
    void freeAllTextures();
    AudioWrapper *loadAudio(const std::string &filePath, bool required = false);
    void freeAudio(AudioWrapper *audioWrapper);
    void freeAllAudio();
    AnimationClip *loadAnimationClip(const std::string &filePath);
    AnimationClip *loadAnimationClip(const std::string &filePath, platformator::ReferenceResolver &resolver,
                                     bool required);
    void freeAnimationClip(AnimationClip *animationClip);
    void freeAllAnimationClips();

    void loadScene(Scene &scene);
    void saveScene(const Scene &scene);

    BaseObject *getObjectById(int id) const;
    const Eigen::Vector2f &getGravityVector() const;
    const Eigen::Vector2f &getGravityVectorNormalized() const;

  private:
    Phase phase = Phase::Idle;
    std::thread::id mainThread = std::this_thread::get_id();
    platformator_detail::ObjectRegistry registry;
    bool shutdownComplete = false;
    bool shuttingDown = false;

    struct SceneRequest
    {
        // A null path requests clearing; no pending request means no scene work.
        std::optional<std::string> path;
    };

    std::optional<SceneRequest> pendingSceneRequest;
    bool preparingScene = false;
    int64_t nextFileId = 0;
    void assignFileId(BaseObject &object);
    void reconcilePending(std::exception_ptr &error);
    void drainRetirements(std::exception_ptr &error);
    std::vector<Component *> reconciliations;
    std::vector<Component *> reconciliationDrain;
    std::vector<Component *> retiredComponents;
    std::vector<GameObject *> dirtyTransforms;
    size_t nextHierarchyOrder = 0;
    bool hierarchyDirty = true;
    bool transformOrderNeeded = false;
    uint64_t transformStamp = 0;
    void queueTransform(GameObject *object, bool refreshImmediate = true);
    void cancelTransform(GameObject *object);
    void synchronizeTransforms();
    void rebuildHierarchyOrder(GameObject *object);
    void synchronizeSubtree(GameObject *object);
    void publishRenderGeometryChange(GameObject *object);
    void queueReconciliation(Component *component);
    void retireComponent(Component *component);
    void runDestructionCallbacks(ScriptComponent *script, std::exception_ptr &error);
    std::unique_ptr<SDLWindow> window;
    std::vector<GameObject *> gameObjects;
    std::vector<Animator *> animatorComponents;
    std::vector<ScriptComponent *> scriptComponents;
    platformator_detail::AssetRequestAliases textureAliases, audioAliases, animationAliases;
    std::unordered_map<std::string, std::unique_ptr<TextureWrapper>> textureCache;
    std::unordered_map<std::string, std::unique_ptr<AudioWrapper>> audioCache;
    std::unordered_map<std::string, std::unique_ptr<AnimationClip>> animationClipCache;
    std::unordered_map<int, BaseObject *> idToObjectMap;
    std::vector<GameObject *> gameObjectsToDelete;
    std::vector<ScriptComponent *> pendingStartScripts;

    std::unique_ptr<PhysicsManager> physicsManager;


    void createMainCameraIfNoMainCameraExists();
    void registerGameObjectSubtree(GameObject *gameObject);
    void runSimulationStep(double timeDelta);
    void startDeletingGameObject(GameObject *gameObject);

    void removeGameObject(GameObject *gameObject);

    void notifyPhysicsManagerOfComponentAdded(Component *component);
    void notifyWindowOfComponentAdded(Component *component);

    void notifyPhysicsManagerOfComponentRemoved(Component *component);
    void notifyWindowOfComponentRemoved(Component *component);

    void notifyRuntimeOfComponentAdded(Component *component);
    void notifyRuntimeOfComponentRemoved(Component *component);
    void notifyComponentAdded(Component *component);

    PhysicsManager *getPhysicsManager() const;
    void fixedUpdateScriptComponents(double timeDelta);
    void updateScriptComponents(double timeDelta);
    void lateUpdateScriptComponents(double timeDelta);
    void updateAnimatorComponents(double timeDelta);
    void startPendingScripts();

    void queueScriptStart(ScriptComponent *script);
    void cancelScriptStart(ScriptComponent *script);
    const std::vector<GameObject *> &getGameObjects() const;
    void simulateFrameWithCustomCallback(const std::function<void(double)> &customCallback, double timeDelta);
    void runSimulationStepWithCustomCallback(const std::function<void(double)> &customCallback,
                                             double timeDelta);
    bool simulateAndRenderFrameWithCustomCallback(const std::function<void(double)> &customCallback,
                                                  double timeDelta);
};
