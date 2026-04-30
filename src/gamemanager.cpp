#include "gamemanager.h"

#include <algorithm>

#include "animator.h"
#include "audio.h"
#include "scriptcomponent.h"
#include "texturewrapper.h"
#include "animationclip.h"
#include <fstream>

GameManager::GameManager() : window(new SDLWindow()), gameObjects(), animatorComponents(), scriptComponents(), textureCache(), audioCache(), animationClipCache(), gameObjectsToDelete(), scenes(), physicsManager(new PhysicsManager()), deltaTime(0.0), lastUpdateTime(0.0)
{
    lastUpdateTime = static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()) / 1000.0;
    // initializeMainCamera();
}

GameManager::~GameManager()
{
    for (GameObject *gameObject : gameObjects)
    {
        startDeletingGameObject(gameObject);
    }
    gameObjects.clear();

    deleteMarkedGameObjects();
    freeAllAnimationClips();
    freeAllTextures();
    freeAllAudio();

    delete physicsManager;
    delete window;
}

void GameManager::addScene(const Scene &scene)
{
    scenes.push_back(scene);
}

std::vector<Scene> &GameManager::getScenes()
{
    return scenes;
}

void GameManager::loadScene(Scene &scene)
{
    std::vector<GameObject *> loadedObjects = scene.loadScene();

    for (GameObject *gameObject : loadedObjects)
    {
        addGameObject(gameObject);
    }
    createMainCameraIfNoMainCameraExists();

    for (GameObject *gameObject : loadedObjects)
    {
        if (!gameObject->getActive())
        {
            continue;
        }

        ScriptComponent *scriptComponent = gameObject->getComponent<ScriptComponent>();
        if (scriptComponent == nullptr)
        {
            continue;
        }

        for (Behavior *behavior : scriptComponent->getBehaviors())
        {
            addStartedBehavior(behavior);
        }
    }
}

void GameManager::saveScene(const Scene &scene)
{
    scene.saveScene(gameObjects);
}

void GameManager::createMainCameraIfNoMainCameraExists()
{
    if (window->getMainCamera() != nullptr)
    {
        return;
    }

    GameObject *mainCameraGameObject = new GameObject();
    Camera *mainCameraComponent = new Camera(mainCameraGameObject);
    mainCameraGameObject->addComponentInternal(mainCameraComponent);

    addGameObject(mainCameraGameObject);

    window->setMainCamera(mainCameraComponent);
}

GameObject *GameManager::addGameObject(GameObject *gameObject)
{
    gameObject->setGameManagerIteratorIndex(gameObjects.size());
    gameObjects.push_back(gameObject);
    gameObject->setIsRegisteredInGameManager(true);
    gameObject->addComponentsToGameManager();

    return gameObject;
}

void GameManager::removeGameObject(GameObject *gameObject)
{
    if (gameObject->getIsMarkedForDeletion() || !gameObject->getIsRegisteredInGameManager())
    {
        return;
    }

    size_t lastIndex = gameObjects.size() - 1;
    size_t removeIndex = gameObject->gameManagerIteratorIndex;
    if (removeIndex != lastIndex)
    {
        GameObject *movedGameObject = gameObjects.back();
        gameObjects[removeIndex] = movedGameObject;
        movedGameObject->setGameManagerIteratorIndex(removeIndex);
    }
    gameObjects.pop_back();

    startDeletingGameObject(gameObject);
}

void GameManager::startDeletingGameObject(GameObject *gameObject)
{
    gameObject->setIsMarkedForDeletion(true);
    gameObject->removeComponentsFromGameManager();
    gameObject->setIsRegisteredInGameManager(false);
    gameObjectsToDelete.push_back(gameObject);
}

bool GameManager::removeGameObject(std::string name)
{
    for (GameObject *gameObject : gameObjects)
    {
        if (gameObject->getName() == name)
        {
            removeGameObject(gameObject);
            return true;
        }
    }
    return false;
}

GameObject *GameManager::getGameObject(std::string name)
{
    for (GameObject *gameObject : gameObjects)
    {
        if (gameObject->getName() == name)
        {
            return gameObject;
        }
    }

    return nullptr;
}

std::vector<GameObject *> &GameManager::getGameObjects()
{
    return gameObjects;
}

void GameManager::setPhysicsManager(PhysicsManager *physicsManager)
{
    this->physicsManager = physicsManager;
}

PhysicsManager *GameManager::getPhysicsManager() const
{
    return physicsManager;
}

SDLWindow *GameManager::getWindow() const
{
    return window;
}

void GameManager::updateDeltaTime()
{
    double currentTime = static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()) / 1000.0;
    deltaTime = currentTime - lastUpdateTime;
    lastUpdateTime = currentTime;
}

void GameManager::delay()
{
    double elapsedTime = static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()) / 1000.0 - lastUpdateTime;
    if (elapsedTime < FRAME_TIME)
    {
        SDL_Delay(static_cast<Uint32>((FRAME_TIME - elapsedTime) * 1000.0));
    }
}

void GameManager::loop()
{
    while (window->isRunning())
    {
        updateDeltaTime();
        window->handleSDLEvents(deltaTime);

        if (window->shouldSimulateFrame())
        {
            window->clearDebugObjects();
            if (window->getIsFrameAdvanceMode())
            {
                deltaTime = FRAME_TIME;
            }

            triggerStartedBehaviors();
            fixedUpdateScriptComponents(deltaTime);
            physicsManager->checkForCollisions();
            physicsManager->applyPhysics(deltaTime);
            physicsManager->resolveCollisions(deltaTime);
            physicsManager->applyMovement(deltaTime);
            physicsManager->handlePendingPhysicsEvents(deltaTime);
            updateScriptComponents(deltaTime);
            updateAnimatorComponents(deltaTime);
            lateUpdateScriptComponents(deltaTime);
        }

        window->render();
        window->clearAdvanceFrameRequest();
        deleteMarkedGameObjects();
        delay();
    }
}

void GameManager::simulateFrame(double timeDelta)
{
    triggerStartedBehaviors();
    fixedUpdateScriptComponents(timeDelta);
    physicsManager->checkForCollisions();
    physicsManager->applyPhysics(timeDelta);
    physicsManager->resolveCollisions(timeDelta);
    physicsManager->applyMovement(timeDelta);
    physicsManager->handlePendingPhysicsEvents(timeDelta);
    updateScriptComponents(timeDelta);
    updateAnimatorComponents(timeDelta);
    lateUpdateScriptComponents(timeDelta);
    deleteMarkedGameObjects();
}

TextureWrapper *GameManager::loadTexture(const std::string &filePath)
{
    auto it = textureCache.find(filePath);
    if (it != textureCache.end())
    {
        return &it->second;
    }

    SDL_Texture *texture = IMG_LoadTexture(window->getRenderer(), filePath.c_str());
    if (!texture)
    {
        printf("Failed to load %s: %s\n", filePath.c_str(), SDL_GetError());
        return nullptr;
    }

    // need this so that TextureWrapper is made in-place (so it doesn't destroy the texture)
    auto [insertedIt, inserted] = textureCache.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(filePath),
        std::forward_as_tuple(texture, filePath)); // uses the TextureWrapper constructor that takes an SDL_Texture* and a file path

    return &insertedIt->second;
}

void GameManager::freeTexture(TextureWrapper *textureWrapper)
{
    auto it = textureCache.find(textureWrapper->getFilePath());
    if (it != textureCache.end())
    {
        textureCache.erase(it);
    }
}

void GameManager::freeAllTextures()
{
    textureCache.clear();
}

GameObject *GameManager::createGameObject()
{
    GameObject *gameObject = new GameObject();

    addGameObject(gameObject);
    return gameObject;
}

void GameManager::destroyGameObject(GameObject *gameObject)
{
    removeGameObject(gameObject);
}

void GameManager::notifyComponentAdded(Component *component)
{
    if (component == nullptr)
    {
        return;
    }

    notifyPhysicsManagerOfComponentAdded(component);
    notifyWindowOfComponentAdded(component);
    notifyRuntimeOfComponentAdded(component);
}

void GameManager::notifyComponentRemoved(Component *component)
{
    if (component == nullptr)
    {
        return;
    }

    notifyPhysicsManagerOfComponentRemoved(component);
    notifyWindowOfComponentRemoved(component);
    notifyRuntimeOfComponentRemoved(component);
}

void GameManager::notifyRuntimeOfComponentAdded(Component *component)
{
    if (component == nullptr || !component->getGameObject()->getActive())
    {
        return;
    }

    if (component->getType() == ComponentType::ANIMATOR)
    {
        Animator *animator = static_cast<Animator *>(component);
        size_t index = animatorComponents.size();
        animator->setGameManagerIndex(index);
        animatorComponents.push_back(animator);
    }
    else if (component->getType() == ComponentType::SCRIPT)
    {
        ScriptComponent *scriptComponent = static_cast<ScriptComponent *>(component);
        size_t index = scriptComponents.size();
        scriptComponent->setGameManagerIndex(index);
        scriptComponents.push_back(scriptComponent);
    }
}

void GameManager::notifyRuntimeOfComponentRemoved(Component *component)
{
    if (component == nullptr)
    {
        return;
    }

    if (component->getType() == ComponentType::ANIMATOR)
    {
        Animator *animator = static_cast<Animator *>(component);
        size_t removeIndex = animator->getGameManagerIndex();

        if (removeIndex == SIZE_MAX)
        {
            return;
        }

        size_t lastIndex = animatorComponents.size() - 1;

        if (removeIndex != lastIndex)
        {
            Animator *movedAnimator = animatorComponents.back();
            animatorComponents[removeIndex] = movedAnimator;
            movedAnimator->setGameManagerIndex(removeIndex);
        }

        animatorComponents.pop_back();
        animator->setGameManagerIndex(SIZE_MAX);
    }
    else if (component->getType() == ComponentType::SCRIPT)
    {
        ScriptComponent *scriptComponent = static_cast<ScriptComponent *>(component);
        size_t removeIndex = scriptComponent->getGameManagerIndex();

        if (removeIndex == SIZE_MAX)
        {
            return;
        }

        size_t lastIndex = scriptComponents.size() - 1;

        if (removeIndex != lastIndex)
        {
            ScriptComponent *movedScriptComponent = scriptComponents.back();
            scriptComponents[removeIndex] = movedScriptComponent;
            movedScriptComponent->setGameManagerIndex(removeIndex);
        }

        scriptComponents.pop_back();
        scriptComponent->setGameManagerIndex(SIZE_MAX);
    }
    else if (component->getType() == ComponentType::AUDIO)
    {
        static_cast<Audio *>(component)->stop();
    }
}

void GameManager::notifyPhysicsManagerOfComponentAdded(Component *component)
{
    if (component == nullptr || physicsManager == nullptr)
    {
        return;
    }

    if (component->getType() == ComponentType::COLLIDER)
    {
        physicsManager->addColliderComponent((Collider *)component);
    }
    else if (component->getType() == ComponentType::RIGID_BODY)
    {
        physicsManager->addRigidBodyComponent((Rigidbody *)component);
    }
}

void GameManager::notifyWindowOfComponentAdded(Component *component)
{
    if (component == nullptr || window == nullptr)
    {
        return;
    }

    if (component->getType() == ComponentType::SPRITE)
    {
        window->addSpriteComponent((Sprite *)component);
    }

    if (component->getType() == ComponentType::CAMERA)
    {
        window->setMainCamera((Camera *)component);
    }
}

void GameManager::notifyPhysicsManagerOfComponentRemoved(Component *component)
{
    if (component == nullptr || physicsManager == nullptr)
    {
        return;
    }

    if (component->getType() == ComponentType::COLLIDER)
    {
        physicsManager->removeColliderComponent((Collider *)component);
    }
    else if (component->getType() == ComponentType::RIGID_BODY)
    {
        physicsManager->removeRigidBodyComponent((Rigidbody *)component);
    }
}

void GameManager::notifyWindowOfComponentRemoved(Component *component)
{
    if (component == nullptr || window == nullptr)
    {
        return;
    }

    if (component->getType() == ComponentType::SPRITE)
    {
        window->removeSpriteComponent((Sprite *)component);
    }

    if (component->getType() == ComponentType::CAMERA && window->getMainCamera() == (Camera *)component)
    {
        window->setMainCamera(nullptr);
    }
}

void GameManager::deleteMarkedGameObjects()
{
    for (GameObject *gameObject : gameObjectsToDelete)
    {
        delete gameObject;
    }
    gameObjectsToDelete.clear();
}

void GameManager::fixedUpdateScriptComponents(double timeDelta)
{
    for (ScriptComponent *scriptComponent : scriptComponents)
    {
        if (!scriptComponent->getGameObject()->getActive())
        {
            continue;
        }

        scriptComponent->fixedUpdate(timeDelta);
    }
}

void GameManager::updateScriptComponents(double timeDelta)
{
    for (ScriptComponent *scriptComponent : scriptComponents)
    {
        if (!scriptComponent->getGameObject()->getActive())
        {
            continue;
        }

        scriptComponent->update(timeDelta);
    }
}

void GameManager::lateUpdateScriptComponents(double timeDelta)
{
    for (ScriptComponent *scriptComponent : scriptComponents)
    {
        if (!scriptComponent->getGameObject()->getActive())
        {
            continue;
        }

        scriptComponent->lateUpdate(timeDelta);
    }
}

void GameManager::updateAnimatorComponents(double timeDelta)
{
    for (Animator *animator : animatorComponents)
    {
        if (!animator->getGameObject()->getActive())
        {
            continue;
        }

        animator->update(timeDelta);
    }
}

AudioWrapper *GameManager::loadAudio(const std::string &filePath)
{
    auto it = audioCache.find(filePath);
    if (it != audioCache.end())
    {
        return &it->second;
    }

    MIX_Audio *audio = MIX_LoadAudio(window->getMixer(), filePath.c_str(), true);
    if (!audio)
    {
        printf("Failed to load %s: %s\n", filePath.c_str(), SDL_GetError());
        return nullptr;
    }

    // need this so that AudioWrapper is made in-place (so it doesn't destroy the audio)
    auto [insertedIt, inserted] = audioCache.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(filePath),
        std::forward_as_tuple(audio, filePath)); // uses the AudioWrapper constructor that takes an MIX_Audio* and a file path

    return &insertedIt->second;
}

void GameManager::freeAudio(AudioWrapper *audioWrapper)
{
    auto it = audioCache.find(audioWrapper->getFilePath());
    if (it != audioCache.end())
    {
        audioCache.erase(it);
    }
}

void GameManager::freeAllAudio()
{
    audioCache.clear();
}

AnimationClip *GameManager::loadAnimationClip(const std::string &filePath)
{
    auto it = animationClipCache.find(filePath);
    if (it != animationClipCache.end())
    {
        return &it->second;
    }

    std::ifstream file(filePath);
    if (!file.is_open())
    {
        printf("Failed to load %s\n", filePath.c_str());
        return nullptr;
    }
    nlohmann::json json = nlohmann::json::parse(file);
    file.close();

    AnimationClip animationClip = json.get<AnimationClip>();

    auto [insertedIt, inserted] = animationClipCache.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(filePath),
        std::forward_as_tuple(animationClip));

    return &insertedIt->second;
}

void GameManager::freeAnimationClip(AnimationClip *animationClip)
{
    if (animationClip == nullptr)
    {
        return;
    }

    auto it = animationClipCache.find(animationClip->getFilePath());
    if (it != animationClipCache.end())
    {
        animationClipCache.erase(it);
    }
}

void GameManager::freeAllAnimationClips()
{
    animationClipCache.clear();
}

void GameManager::addStartedBehavior(Behavior *behavior)
{
    startedBehaviors.push_back(behavior);
}

void GameManager::removeStartedBehavior(Behavior *behavior)
{
    auto it = std::remove(startedBehaviors.begin(), startedBehaviors.end(), behavior);
    if (it != startedBehaviors.end())
    {
        startedBehaviors.erase(it, startedBehaviors.end());
    }
}

void GameManager::triggerStartedBehaviors()
{
    for (Behavior *behavior : startedBehaviors)
    {
        behavior->start();
    }
    startedBehaviors.clear();
}