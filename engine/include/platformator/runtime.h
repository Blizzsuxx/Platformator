#pragma once

#include <memory>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include <functional>
#include <filesystem>

#include "platformator/runtimeoptions.h"
#include "platformator/objecthandle.h"
#include "platformator/collisiontypes.h"
#include <cstdint>
#include <optional>

class BaseObject;
class Camera;
class GameManager;
class GameObject;
class Scene;
class SDLWindow;
struct AnimationClip;
class AudioWrapper;
class Template;
class TextureWrapper;
class PathManager;

namespace platformator_detail
{
class RuntimeAccess;
}

namespace platformator
{
class Runtime
{
  public:
    explicit Runtime(const RuntimeOptions &runtimeOptions = RuntimeOptions{});
    Runtime(const Runtime &) = delete;
    Runtime &operator=(const Runtime &) = delete;
    ~Runtime();

    static Runtime &current();
    static Runtime *tryGetCurrent();
    const std::filesystem::path &getAssetRoot() const;

    GameObject *createGameObject();
    void destroyGameObject(GameObject *gameObject);
    void clearScene();
    // Stop and release all independent fire-and-forget tracks.
    void stopTransientAudio();
    void requestClearScene();
    void requestLoadScene(const std::string &sceneFilePath);
    GameObject *getGameObject(const std::string &name) const;
    BaseObject *getObjectById(int id) const;
    BaseObject *resolveHandle(ObjectHandle handle, platformator_detail::ObjectKind kind) const;
    uint64_t getRuntimeSerial() const;
    Camera *getMainCamera() const;
    void createMainCameraIfNoMainCameraExists();
    // Nonempty asset requests must exist and load successfully; failures throw.
    // A retained cached resource is reused without rechecking its filesystem path.
    AnimationClip *loadAnimationClip(const std::string &filePath) const;
    AudioWrapper *loadAudio(const std::string &filePath) const;
    Template *loadTemplate(const std::string &filePath) const;
    void loadScene(Scene &scene);
    void loadScene(const std::string &sceneFilePath);
    TextureWrapper *loadTexture(const std::string &filePath) const;
    void saveScene(Scene &scene);
    void saveScene();
    void saveScene(const std::string &sceneFilePath);
    void run();
    // Runs the normal interactive loop and invokes a bounded, nonblocking
    // owner-thread service before each frame.
    void run_with_owner_service(const std::function<bool()> &ownerThreadService);
    // The session overload provides the owned SDL window for control requests
    // and reports each completed fixed simulation step on the owner thread
    // (after commitMutations finishes).
    void run_with_owner_service(
        const std::function<bool(SDLWindow &)> &ownerThreadService,
        const std::function<void(std::uint64_t)> &afterSimulationStep);
    void simulateFrame(double timeDelta);
    bool simulateAndRenderFrame(double timeDelta);
    // Invokes prePhysicsStepHook as a pre-physics step hook (after pending script
    // start() and before fixedUpdate/physics/update/lateUpdate/commitMutations).
    bool simulateAndRenderFrameWithCustomCallback(double timeDelta,
                                                  const std::function<void(double)> &prePhysicsStepHook);
    // Poll native or embedded Game view input. Call on the runtime owner thread.
    bool isKeyDown(SDL_Scancode key) const;
    SDL_MouseButtonFlags getMouseState(float *x = nullptr, float *y = nullptr) const;
    SDL_Window *getWindowHandle() const;
    SDL_Renderer *getRenderer() const;
    SDL_Color getClearColor() const;
    void setClearColor(const SDL_Color &color);
    void setClearColor(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255);
    const std::vector<GameObject *> &getAllGameObjects() const;
    // Invokes prePhysicsStepHook as a pre-physics step hook (after pending script
    // start() and before fixedUpdate/physics/update/lateUpdate/commitMutations).
    void simulateFrameWithCustomCallback(double timeDelta, const std::function<void(double)> &prePhysicsStepHook);

    // Queries the synchronized physics world (Grid + AABB and published collider
    // geometry from the latest step commit, scene load, or idle reconciliation).
    // Synced entry points: Behavior::fixedUpdate(double) (observes the prior step's
    // committed physics/script state and supports normal mutations such as destroy()),
    // run_with_owner_service(..., afterSimulationStep), or owner-thread callers after
    // simulateFrame* / simulateAndRenderFrame* returns. The custom callback in
    // simulate*WithCustomCallback runs before fixedUpdate/physics and also observes
    // the previously committed sync state. Geometry edits queued during a step become
    // visible after the next sync point (broadPhase or end-of-step commit). Thread-safe
    // for concurrent read-only queries on a stable scene while no mutations occur.
    std::optional<RaycastHit> raycast(const Eigen::Vector2f &origin, const Eigen::Vector2f &direction,
                                      float maxDistance, uint64_t collisionMask = ~uint64_t{0},
                                      bool includeTriggers = false) const;
    bool raycast(const Eigen::Vector2f &origin, const Eigen::Vector2f &direction, float maxDistance,
                 RaycastHit &outHit, uint64_t collisionMask = ~uint64_t{0},
                 bool includeTriggers = false) const;
    std::vector<RaycastHit> raycastAll(const Eigen::Vector2f &origin, const Eigen::Vector2f &direction,
                                       float maxDistance, uint64_t collisionMask = ~uint64_t{0},
                                       bool includeTriggers = false) const;

  private:
    friend class ::GameManager;
    friend class platformator_detail::RuntimeAccess;

    static Runtime *&activeRuntime();
    GameManager &getGameManager() const;

    std::unique_ptr<PathManager> paths;
    std::unique_ptr<GameManager> gameManager;
    uint64_t smokeStepLimit = 0;
    std::string currentSceneFilePath;
};
} // namespace platformator

#include "platformator/runner.h"
