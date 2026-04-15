#include "gamemanager.h"
#include "texturewrapper.h"

GameManager::GameManager() : window(new SDLWindow()), gameObjects(), textureCache(), gameObjectsToDelete(), scenes(), physicsManager(new PhysicsManager()), deltaTime(0.0), lastUpdateTime(0.0)
{
    lastUpdateTime = static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()) / 1000.0;
    // initializeMainCamera();
}

GameManager::~GameManager()
{
    for (std::list<GameObject *>::iterator it = gameObjects.begin(); it != gameObjects.end(); ++it)
    {
        startDeletingGameObject(*it);
    }
    gameObjects.clear();

    deleteMarkedGameObjects();
    freeAllTextures();

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
    gameObjects.push_back(gameObject);
    gameObject->setGameManagerIterator(std::prev(gameObjects.end()));
    gameObject->setIsRegisteredInGameManager(true);

    return gameObject;
}

void GameManager::removeGameObject(GameObject *gameObject)
{
    if (gameObject == nullptr || !gameObject->isRegisteredInGameManager)
    {
        return;
    }

    gameObjects.erase(gameObject->gameManagerIterator);
    gameObject->isRegisteredInGameManager = false;

    startDeletingGameObject(gameObject);
}

void GameManager::startDeletingGameObject(GameObject *gameObject)
{
    if (gameObject == nullptr || gameObject->getIsMarkedForDeletion())
    {
        return;
    }

    notifyComponentRemoved(gameObject->getComponent(ComponentType::COLLIDER));
    notifyComponentRemoved(gameObject->getComponent(ComponentType::RIGID_BODY));
    notifyComponentRemoved(gameObject->getComponent(ComponentType::SPRITE));
    notifyComponentRemoved(gameObject->getComponent(ComponentType::CAMERA));

    gameObject->setIsMarkedForDeletion(true);
    gameObject->isRegisteredInGameManager = false;
    gameObjectsToDelete.push_back(gameObject);
}

bool GameManager::removeGameObject(std::string name)
{
    for (std::list<GameObject *>::iterator it = gameObjects.begin(); it != gameObjects.end(); ++it)
    {
        if ((*it)->getName() == name)
        {
            removeGameObject(*it);
            return true;
        }
    }

    return false;
}

GameObject *GameManager::getGameObject(std::string name)
{
    for (std::list<GameObject *>::iterator it = gameObjects.begin(); it != gameObjects.end(); ++it)
    {
        if ((*it)->getName() == name)
        {
            return *it;
        }
    }

    return nullptr;
}

std::list<GameObject *> &GameManager::getGameObjects()
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
        window->handleEvents();

        if (window->shouldSimulateFrame())
        {
            window->clearDebugObjects();
            if (window->getIsFrameAdvanceMode())
            {
                deltaTime = FRAME_TIME;
            }

            physicsManager->checkForCollisions();
            physicsManager->applyPhysics(deltaTime);
            physicsManager->resolveCollisions(deltaTime);
            physicsManager->applyMovement(deltaTime);
        }

        window->render();
        window->clearAdvanceFrameRequest();
        deleteMarkedGameObjects();
        delay();
    }
}

void GameManager::simulateFrame(double timeDelta)
{
    if (physicsManager == nullptr)
    {
        deleteMarkedGameObjects();
        return;
    }

    physicsManager->checkForCollisions();
    physicsManager->applyPhysics(timeDelta);
    physicsManager->resolveCollisions(timeDelta);
    physicsManager->applyMovement(timeDelta);
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
    }

    // need this so that TextureWrapper is made in-place (so it doesn't destroy the texture)
    auto [insertedIt, inserted] = textureCache.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(filePath),
        std::forward_as_tuple(texture, filePath)); // uses the TextureWrapper constructor that takes an SDL_Texture* and a file path

    return &insertedIt->second;
}

void GameManager::freeTexture(const std::string &filePath)
{
    auto it = textureCache.find(filePath);
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
}

void GameManager::notifyComponentRemoved(Component *component)
{
    if (component == nullptr)
    {
        return;
    }

    notifyPhysicsManagerOfComponentRemoved(component);
    notifyWindowOfComponentRemoved(component);
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