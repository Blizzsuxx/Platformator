#include "platformator/runtime.h"

#include <memory>
#include <stdexcept>

#include "constants.h"
#include "gamemanager.h"
#include "pathmanager.h"
#include "platformator/scene.h"
#include "sdlwindow.h"

namespace platformator
{
Runtime::Runtime(const RuntimeOptions &runtimeOptions)
{
    if (activeRuntime() != nullptr)
    {
        throw std::logic_error("Only one Platformator runtime can be active at a time.");
    }
    paths = std::make_unique<PathManager>(runtimeOptions.assetRoot);
    smokeStepLimit = runtimeOptions.smokeSteps;
    WindowSettings windowSettings = runtimeOptions.windowSettings;
    if (runtimeOptions.editorSession)
    {
        windowSettings.width = 640;
        windowSettings.height = 360;
        windowSettings.fullscreen = false;
        windowSettings.maximizeOnStartup = false;
        windowSettings.keepAspectRatio = false;
        windowSettings.headless = false;
        windowSettings.hidden = true;
        windowSettings.highPixelDensity = false;
    }
    gameManager = std::make_unique<GameManager>(windowSettings, runtimeOptions.debugSettings);
    activeRuntime() = this;
}

Runtime::~Runtime()
{
    if (activeRuntime() == this)
    {
        // Shutdown callbacks and resource release still have access to the live owner.
        gameManager->shutdown();
        gameManager.reset();
        activeRuntime() = nullptr;
    }
}

Runtime &Runtime::current()
{
    Runtime *runtime = tryGetCurrent();
    if (runtime == nullptr)
    {
        throw std::logic_error("Platformator runtime is not active.");
    }

    return *runtime;
}

Runtime *Runtime::tryGetCurrent()
{
    return activeRuntime();
}

const std::filesystem::path &Runtime::getAssetRoot() const
{
    return paths->getRoot();
}

GameObject *Runtime::createGameObject()
{
    return gameManager->createGameObject();
}

void Runtime::destroyGameObject(GameObject *gameObject)
{
    gameManager->destroyGameObject(gameObject);
}

void Runtime::clearScene()
{
    gameManager->clearObjects();
}

void Runtime::stopTransientAudio()
{
    gameManager->assertCanMutate();
    gameManager->getWindow()->clearTransientAudio();
}

void Runtime::requestClearScene()
{
    gameManager->requestScene(std::nullopt);
}

void Runtime::requestLoadScene(const std::string &path)
{
    gameManager->requestScene(path);
}

GameObject *Runtime::getGameObject(const std::string &name) const
{
    return gameManager->getGameObject(name);
}

BaseObject *Runtime::getObjectById(int id) const
{
    return gameManager->getObjectById(id);
}

uint64_t Runtime::getRuntimeSerial() const
{
    return gameManager->registry.serial();
}

BaseObject *Runtime::resolveHandle(ObjectHandle handle, platformator_detail::ObjectKind kind) const
{
    return gameManager->resolveHandle(handle, kind);
}

Camera *Runtime::getMainCamera() const
{
    return gameManager->getWindow()->getMainCamera();
}

void Runtime::createMainCameraIfNoMainCameraExists()
{
    gameManager->createMainCameraIfNoMainCameraExists();
}

AnimationClip *Runtime::loadAnimationClip(const std::string &filePath) const
{
    return gameManager->loadAnimationClip(filePath);
}

AudioWrapper *Runtime::loadAudio(const std::string &filePath) const
{
    return gameManager->loadAudio(filePath);
}

void Runtime::loadScene(Scene &scene)
{
    gameManager->loadScene(scene);
}

void Runtime::loadScene(const std::string &sceneFilePath)
{
    Scene scene(sceneFilePath);
    gameManager->loadScene(scene);
}

TextureWrapper *Runtime::loadTexture(const std::string &filePath) const
{
    return gameManager->loadTexture(filePath);
}

void Runtime::saveScene(Scene &scene)
{
    gameManager->saveScene(scene);
    currentSceneFilePath = scene.resolvedFilePath().string();
}

void Runtime::saveScene()
{
    if (currentSceneFilePath.empty())
    {
        throw std::logic_error("Platformator runtime has no current scene path to save.");
    }

    saveScene(currentSceneFilePath);
}

void Runtime::saveScene(const std::string &sceneFilePath)
{
    Scene scene(sceneFilePath);
    saveScene(scene);
}

void Runtime::run()
{
    if (smokeStepLimit)
    {
        for (uint64_t step = 0; step < smokeStepLimit; ++step)
        {
            if (gameManager->getWindow()->isHeadless())
            {
                simulateFrame(FIXED_TIME_STEP);
            }
            else if (!simulateAndRenderFrame(FIXED_TIME_STEP))
            {
                break;
            }
        }
        return;
    }
    gameManager->loop();
}

void Runtime::run_with_owner_service(const std::function<bool()> &ownerThreadService)
{
    if (!ownerThreadService)
        throw std::invalid_argument("owner-thread Runtime service callback is required");
    if (smokeStepLimit != 0)
        throw std::logic_error("owner-thread Runtime services cannot be combined with smoke-step mode");
    gameManager->loop([&](SDLWindow &) { return ownerThreadService(); });
}

void Runtime::run_with_owner_service(
    const std::function<bool(SDLWindow &)> &ownerThreadService,
    const std::function<void(std::uint64_t)> &afterSimulationStep)
{
    if (!ownerThreadService)
        throw std::invalid_argument("owner-thread Runtime service callback is required");
    if (smokeStepLimit != 0)
        throw std::logic_error("owner-thread Runtime services cannot be combined with smoke-step mode");
    gameManager->loop(ownerThreadService, afterSimulationStep);
}

void Runtime::simulateFrame(double timeDelta)
{
    gameManager->simulateFrame(timeDelta);
}

void Runtime::simulateFrameWithCustomCallback(double timeDelta,
                                              const std::function<void(double)> &preRenderCallback)
{
    gameManager->simulateFrameWithCustomCallback(preRenderCallback, timeDelta);
}

bool Runtime::simulateAndRenderFrame(double timeDelta)
{
    return gameManager->simulateAndRenderFrame(timeDelta);
}

bool Runtime::simulateAndRenderFrameWithCustomCallback(double timeDelta,
                                                       const std::function<void(double)> &preRenderCallback)
{
    return gameManager->simulateAndRenderFrameWithCustomCallback(preRenderCallback, timeDelta);
}

SDL_Window *Runtime::getWindowHandle() const
{
    return gameManager->getWindow()->getWindow();
}

SDL_Renderer *Runtime::getRenderer() const
{
    return gameManager->getWindow()->getRenderer();
}

Runtime *&Runtime::activeRuntime()
{
    static Runtime *runtime = nullptr;
    return runtime;
}

GameManager &Runtime::getGameManager() const
{
    return *gameManager;
}

const std::vector<GameObject *> &Runtime::getAllGameObjects() const
{
    return gameManager->getGameObjects();
}
} // namespace platformator
