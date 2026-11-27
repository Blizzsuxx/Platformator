#include "platformator/runtime.h"

#include <memory>
#include <stdexcept>

#include "core/constants.h"
#include "core/gamemanager.h"
#include "core/pathmanager.h"
#include "editor/editorprotocol.h"
#include "platformator/scene.h"
#include "rendering/sdlwindow.h"

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
        windowSettings.width = editor_session::EmbeddedFrameWidth;
        windowSettings.height = editor_session::EmbeddedFrameHeight;
        windowSettings.fullscreen = false;
        windowSettings.maximizeOnStartup = false;
        windowSettings.keepAspectRatio = false;
        windowSettings.headless = false;
        windowSettings.hidden = true;
        windowSettings.highPixelDensity = false;
    }
    gameManager = std::make_unique<GameManager>(windowSettings, runtimeOptions.debugSettings);
    // Apply editor pause before loading scene autoplay, including builds with
    // standalone debug controls disabled.
    if (runtimeOptions.editorSession && !gameManager->getWindow()->enableEditorSessionControls())
        throw std::runtime_error(std::string("Cannot initialize editor pause controls: ") + SDL_GetError());
    activeRuntime() = this;
    if (!runtimeOptions.sceneFilePath.empty())
    {
        try
        {
            loadScene(runtimeOptions.sceneFilePath);
        }
        catch (...)
        {
            gameManager->shutdown();
            gameManager.reset();
            activeRuntime() = nullptr;
            throw;
        }
    }
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
    gameManager->getAudioManager()->clearTransientAudio();
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
    return gameManager->assetStore().loadAnimationClip(filePath);
}

AudioWrapper *Runtime::loadAudio(const std::string &filePath) const
{
    return gameManager->assetStore().loadAudio(filePath);
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
    return gameManager->assetStore().loadTexture(filePath);
}

Template *Runtime::loadTemplate(const std::string &filePath) const
{
    return gameManager->assetStore().loadTemplate(filePath);
}

void Runtime::saveScene(Scene &scene)
{
    gameManager->saveScene(scene);
    gameManager->setCurrentSceneFilePath(scene.resolvedFilePath().string());
}

void Runtime::saveScene()
{
    const std::string &currentPath = gameManager->getCurrentSceneFilePath();
    if (currentPath.empty())
    {
        throw std::logic_error("Platformator runtime has no current scene path to save.");
    }

    saveScene(currentPath);
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
    run_with_owner_service([&](SDLWindow &) { return ownerThreadService(); }, {});
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
    gameManager->runSimulationStep(timeDelta);
}

void Runtime::simulateFrameWithCustomCallback(double timeDelta,
                                              const std::function<void(double)> &prePhysicsStepHook)
{
    gameManager->runSimulationStep(timeDelta, prePhysicsStepHook);
}

bool Runtime::simulateAndRenderFrame(double timeDelta)
{
    return gameManager->simulateAndRenderFrame(timeDelta);
}

bool Runtime::simulateAndRenderFrameWithCustomCallback(double timeDelta,
                                                       const std::function<void(double)> &prePhysicsStepHook)
{
    return gameManager->simulateAndRenderFrame(timeDelta, prePhysicsStepHook);
}

bool Runtime::isKeyDown(SDL_Scancode key) const
{
    return gameManager->getWindow()->isKeyDown(key);
}

SDL_MouseButtonFlags Runtime::getMouseState(float *x, float *y) const
{
    return gameManager->getWindow()->getMouseState(x, y);
}

SDL_Window *Runtime::getWindowHandle() const
{
    return gameManager->getWindow()->getWindow();
}

SDL_Renderer *Runtime::getRenderer() const
{
    return gameManager->getWindow()->getRenderer();
}

SDL_Color Runtime::getClearColor() const
{
    return gameManager->getWindow()->getClearColor();
}

void Runtime::setClearColor(const SDL_Color &color)
{
    gameManager->getWindow()->setClearColor(color);
}

void Runtime::setClearColor(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
{
    gameManager->getWindow()->setClearColor(r, g, b, a);
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

std::optional<RaycastHit> Runtime::raycast(const Eigen::Vector2f &origin, const Eigen::Vector2f &direction,
                                           float maxDistance, uint64_t collisionMask, bool includeTriggers) const
{
    return gameManager->getPhysicsManager()->raycast(origin, direction, maxDistance, collisionMask, includeTriggers);
}

bool Runtime::raycast(const Eigen::Vector2f &origin, const Eigen::Vector2f &direction, float maxDistance,
                      RaycastHit &outHit, uint64_t collisionMask, bool includeTriggers) const
{
    return gameManager->getPhysicsManager()->raycast(origin, direction, maxDistance, outHit, collisionMask,
                                                     includeTriggers);
}

std::vector<RaycastHit> Runtime::raycastAll(const Eigen::Vector2f &origin, const Eigen::Vector2f &direction,
                                            float maxDistance, uint64_t collisionMask, bool includeTriggers) const
{
    return gameManager->getPhysicsManager()->raycastAll(origin, direction, maxDistance, collisionMask, includeTriggers);
}
} // namespace platformator
