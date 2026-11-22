#pragma once

#include "assetstore.h"
#include "flatidmap.h"
#include "objectregistry.h"
#include "physicsmanager.h"
#include "platformator/debugsettings.h"
#include "platformator/scene.h"
#include "platformator/windowsettings.h"
#include "sdlwindow.h"

#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class Animator;
class Asset;
class ScriptComponent;

namespace platformator
{
class ReferenceResolver;
class Runtime;
} // namespace platformator

namespace platformator_detail
{
class AssetStore;
class RuntimeAccess;
} // namespace platformator_detail

namespace platformator_editor_detail
{
class EditorPreviewAccess;
}

class GameManager
{
    friend class SceneBuildContext;
    friend class Template;
    friend class platformator::Runtime;
    friend class platformator_detail::AssetStore;
    friend class platformator_detail::RuntimeAccess;
    friend class GameObject;
    friend class Collider;
    friend class ScriptComponent;
    friend class PhysicsManager;
    friend class platformator_editor_detail::EditorPreviewAccess;

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

    GameManager(GameManager &) = delete;
    GameManager &operator=(const GameManager &) = delete;

    GameManager(const WindowSettings &windowSettings, const DebugSettings &debugSettings);
    ~GameManager();

    void assertCanMutate() const
    {
        if (std::this_thread::get_id() != mainThread || shuttingDown || preparingScene ||
            phase == Phase::DestructionCallbacks || phase == Phase::Saving)
        {
            throwCannotMutate();
        }
    }
    void commitLocalMutations();
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

    GameObject *getGameObject(const std::string &name);
    GameObject *createGameObject();
    void destroyGameObject(GameObject *gameObject);
    SDLWindow *getWindow() const;

    void loop(const std::function<bool(SDLWindow &)> &ownerThreadService = {},
              const std::function<void(std::uint64_t)> &afterSimulationStep = {});
    void simulateFrame(double timeDelta);
    bool simulateAndRenderFrame(double timeDelta);

    platformator_detail::AssetStore &assetStore() noexcept
    {
        return assets;
    }
    const platformator_detail::AssetStore &assetStore() const noexcept
    {
        return assets;
    }

    void loadScene(const Scene &scene);
    void saveScene(const Scene &scene);

    BaseObject *getObjectById(int id) const;

  private:
    struct SceneRequest
    {
        // A null path requests clearing; no pending request means no scene work.
        std::optional<std::string> path;
    };

    Phase phase = Phase::Idle;
    std::thread::id mainThread = std::this_thread::get_id();
    platformator_detail::ObjectRegistry registry;
    bool shutdownComplete = false;
    bool shuttingDown = false;
    std::uint64_t simulationStepNumber = 0;

    std::optional<SceneRequest> pendingSceneRequest;
    bool preparingScene = false;
    int64_t nextFileId = 0;

    std::unique_ptr<SDLWindow> window;
    std::unique_ptr<PhysicsManager> physicsManager;
    platformator_detail::AssetStore assets;

    std::vector<GameObject *> gameObjects;
    std::vector<Animator *> animatorComponents;
    std::vector<ScriptComponent *> scriptComponents;
    std::vector<ScriptComponent *> fixedUpdateScripts;
    std::vector<ScriptComponent *> updateScripts;
    std::vector<ScriptComponent *> lateUpdateScripts;
    platformator_detail::FlatIdMap idToObjectMap;

    std::vector<Component *> reconciliations;
    std::vector<Component *> reconciliationDrain;
    std::vector<Component *> retiredComponents;
    std::vector<GameObject *> gameObjectsToDelete;
    std::vector<ScriptComponent *> pendingStartScripts;
    std::vector<GameObject *> dirtyTransforms;
    uint64_t transformStamp = 0;

    // AssetStore helpers
    void tagAssetRuntime(Asset &asset) const;
    void loadTemplateDocument(Template &tpl, const nlohmann::json &document);

    // Object & scene lifecycle
    void assignFileId(BaseObject &object);
    void createMainCameraIfNoMainCameraExists();
    [[noreturn]] void throwCannotMutate() const;
    void registerGameObjectSubtree(GameObject *gameObject);
    void removeGameObject(GameObject *gameObject);
    void startDeletingGameObject(GameObject *gameObject);
    void clearAllSceneObjects(std::exception_ptr &error);
    void commitSceneTransition(SceneRequest selected);
    void reconcilePending(std::exception_ptr &error);
    void drainRetirements(std::exception_ptr &error);
    void runDestructionCallbacks(ScriptComponent *script, std::exception_ptr &error);

    // Component subsystem registration
    void notifyComponentAdded(Component *component);
    void queueReconciliation(Component *component);
    void retireComponent(Component *component);
    void syncScriptUpdateLists(ScriptComponent *script);
    void removeScriptFromUpdateLists(ScriptComponent *script);
    void registerComponentInSystems(Component *component);
    void unregisterComponentFromSystems(Component *component);

    // Transform synchronization
    void queueTransform(GameObject *object, bool refreshImmediate = true);
    void cancelTransform(GameObject *object);
    void synchronizeTransforms();
    void synchronizeSubtree(GameObject *object);
    void publishRenderGeometryChange(GameObject *object);

    // Simulation step & script dispatch
    PhysicsManager *getPhysicsManager() const;
    void fixedUpdateScriptComponents(double timeDelta);
    void updateScriptComponents(double timeDelta);
    void lateUpdateScriptComponents(double timeDelta);
    void updateAnimatorComponents(double timeDelta);
    void startPendingScripts();
    void queueScriptStart(ScriptComponent *script);
    void cancelScriptStart(ScriptComponent *script);

    const std::vector<GameObject *> &getGameObjects() const;
    void runSimulationStep(double timeDelta);
    void simulateFrameWithCustomCallback(const std::function<void(double)> &customCallback, double timeDelta);
    void runSimulationStepWithCustomCallback(const std::function<void(double)> &customCallback, double timeDelta);
    bool simulateAndRenderFrameWithCustomCallback(const std::function<void(double)> &customCallback, double timeDelta);
};
