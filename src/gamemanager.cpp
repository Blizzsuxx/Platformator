#include "gamemanager.h"
#include "texturewrapper.h"

GameManager::GameManager() : window(new SDLWindow()), gameObjects(), textureCache(), gameObjectsToDelete(), physicsManager(new PhysicsManager()), deltaTime(0.0), lastUpdateTime(0.0)
{
    lastUpdateTime = static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()) / 1000.0;
    initializeMainCamera();
}

GameManager::~GameManager()
{
    for (std::list<GameObject *>::iterator it = gameObjects.begin(); it != gameObjects.end(); ++it)
    {
        startDeletingGameObject(*it);
    }
    gameObjects.clear();

    freeAllTextures();
    deleteMarkedGameObjects();

    delete physicsManager;
    delete window;
}

void GameManager::initializeMainCamera()
{
    GameObject *mainCameraGameObject = new GameObject();
    mainCameraGameObject->setName("MainCamera");
    mainCameraGameObject->setTag("MainCamera");

    Camera *mainCameraComponent = new Camera(mainCameraGameObject);
    mainCameraGameObject->addComponentInternal(mainCameraComponent);

    addGameObject(mainCameraGameObject);

    window->setMainCamera(mainCameraComponent);
}

GameObject *GameManager::addGameObject(GameObject *gameObject)
{
    gameObjects.push_back(gameObject);

    notifyComponentAdded(gameObject->getComponent(ComponentType::COLLIDER));
    notifyComponentAdded(gameObject->getComponent(ComponentType::RIGID_BODY));
    notifyComponentAdded(gameObject->getComponent(ComponentType::SPRITE));

    return gameObject;
}

void GameManager::removeGameObject(GameObject *gameObject)
{
    gameObjects.remove(gameObject);

    startDeletingGameObject(gameObject);
}

void GameManager::startDeletingGameObject(GameObject *gameObject)
{
    notifyComponentRemoved(gameObject->getComponent(ComponentType::COLLIDER));
    notifyComponentRemoved(gameObject->getComponent(ComponentType::RIGID_BODY));
    notifyComponentRemoved(gameObject->getComponent(ComponentType::SPRITE));

    gameObject->setIsMarkedForDeletion(true);
    gameObjectsToDelete.push_back(gameObject);
}

bool GameManager::removeGameObject(std::string name)
{
    for (std::list<GameObject *>::iterator it = gameObjects.begin(); it != gameObjects.end(); ++it)
    {
        if ((*it)->getName() == name)
        {
            GameObject *gameObject = *it;
            gameObjects.erase(it);
            startDeletingGameObject(gameObject);
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
        physicsManager->applyPhysics(deltaTime);
        physicsManager->checkForCollisions();
        physicsManager->resolveCollisions();
        window->render();
        deleteMarkedGameObjects();
        delay();
    }
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
        SDL_DestroyTexture(it->second.getTexture());
        textureCache.erase(it);
    }
}

void GameManager::freeAllTextures()
{
    for (auto &pair : textureCache)
    {
        SDL_DestroyTexture(pair.second.getTexture());
    }
    textureCache.clear();
}

GameObject *GameManager::createGameObject()
{
    GameObject *gameObject = new GameObject();

    addGameObject(gameObject);
    return gameObject;
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
}

void GameManager::deleteMarkedGameObjects()
{
    for (std::list<GameObject *>::iterator it = gameObjectsToDelete.begin(); it != gameObjectsToDelete.end(); ++it)
    {
        delete *it;
    }
    gameObjectsToDelete.clear();
}