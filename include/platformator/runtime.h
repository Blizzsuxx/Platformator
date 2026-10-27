#pragma once

#include <memory>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include <functional>
#include <filesystem>

#include "platformator/runtimeoptions.h"
#include "platformator/objecthandle.h"

class BaseObject;
class Camera;
class GameManager;
class GameObject;
class Scene;
class AnimationClip;
class AudioWrapper;
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
    void loadScene(Scene &scene);
    void loadScene(const std::string &sceneFilePath);
    TextureWrapper *loadTexture(const std::string &filePath) const;
    void saveScene(Scene &scene);
    void saveScene();
    void saveScene(const std::string &sceneFilePath);
    void run();
    void simulateFrame(double timeDelta);
    bool simulateAndRenderFrame(double timeDelta);
    bool simulateAndRenderFrameWithCustomCallback(double timeDelta,
                                                  const std::function<void(double)> &preRenderCallback);
    SDL_Window *getWindowHandle() const;
    SDL_Renderer *getRenderer() const;
    const std::vector<GameObject *> &getAllGameObjects() const;
    void simulateFrameWithCustomCallback(double timeDelta, const std::function<void(double)> &preRenderCallback);

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
