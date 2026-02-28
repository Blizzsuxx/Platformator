#include "gamemanager.h"

GameManager::GameManager() : window(new SDLWindow()), gameObjects(), physicsManager(new PhysicsManager()), deltaTime(0.0), lastUpdateTime(0.0)
{
    lastUpdateTime = static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()) / 1000.0;
    initializeMainCamera();
}

GameManager::~GameManager()
{
    for (std::list<GameObject *>::iterator it = gameObjects.begin(); it != gameObjects.end(); ++it)
    {
        deleteGameObject(*it);
    }
    gameObjects.clear();

    delete physicsManager;
    delete window;
}

void GameManager::initializeMainCamera()
{
    GameObject *mainCameraGameObject = new GameObject();
    mainCameraGameObject->setName("MainCamera");
    mainCameraGameObject->setTag("MainCamera");

    Camera *mainCameraComponent = new Camera(mainCameraGameObject);
    mainCameraGameObject->addComponent(mainCameraComponent);

    addGameObject(mainCameraGameObject);

    window->setMainCamera(mainCameraComponent);
}

GameObject *GameManager::addGameObject(GameObject *gameObject)
{
    gameObjects.push_back(gameObject);

    if (physicsManager)
    {
        Collider *colliderComponent = (Collider *)gameObject->getComponent(ComponentType::COLLIDER);
        if (colliderComponent)
        {
            physicsManager->addColliderComponent(colliderComponent);
        }

        Rigidbody *rigidBodyComponent = (Rigidbody *)gameObject->getComponent(ComponentType::RIGID_BODY);
        if (rigidBodyComponent)
        {
            physicsManager->addRigidBodyComponent(rigidBodyComponent);
        }
    }

    if (window)
    {
        Sprite *spriteComponent = (Sprite *)gameObject->getComponent(ComponentType::SPRITE);
        if (spriteComponent)
        {
            window->addSpriteComponent(spriteComponent);
        }
    }

    return gameObject;
}

void GameManager::removeGameObject(GameObject *gameObject)
{
    gameObjects.remove(gameObject);

    deleteGameObject(gameObject);
}

void GameManager::deleteGameObject(GameObject *gameObject)
{
    if (physicsManager)
    {
        Collider *colliderComponent = (Collider *)gameObject->getComponent(ComponentType::COLLIDER);
        if (colliderComponent)
        {
            physicsManager->removeColliderComponent(colliderComponent);
        }

        Rigidbody *rigidBodyComponent = (Rigidbody *)gameObject->getComponent(ComponentType::RIGID_BODY);
        if (rigidBodyComponent)
        {
            physicsManager->removeRigidBodyComponent(rigidBodyComponent);
        }
    }

    if (window)
    {
        Sprite *spriteComponent = (Sprite *)gameObject->getComponent(ComponentType::SPRITE);
        if (spriteComponent)
        {
            window->removeSpriteComponent(spriteComponent);
        }
    }

    delete gameObject;
}

bool GameManager::removeGameObject(std::string name)
{
    for (std::list<GameObject *>::iterator it = gameObjects.begin(); it != gameObjects.end(); ++it)
    {
        if ((*it)->getName() == name)
        {
            GameObject *gameObject = *it;
            gameObjects.erase(it);
            deleteGameObject(gameObject);
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

void GameManager::applyPhysics()
{
    if (physicsManager)
    {
        physicsManager->applyPhysics(deltaTime);
    }
}

void GameManager::resolveCollisions()
{
    if (physicsManager)
    {
        physicsManager->checkForCollisions();
        physicsManager->resolveCollisions();
    }
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
        resolveCollisions();
        applyPhysics();
        window->render();
        delay();
    }
}