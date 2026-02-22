// #include "gameobjectmanager.h"

// GameObjectManager::GameObjectManager() : physicsManager(new PhysicsManager())
// {
// }

// GameObjectManager::~GameObjectManager()
// {
//     for (GameObject *gameObject : gameObjects)
//     {
//         delete gameObject;
//     }
// }

// GameObject *GameObjectManager::addGameObject(GameObject *gameObject)
// {
//     gameObjects.push_back(gameObject);

//     if (physicsManager)
//     {
//         Collider *colliderComponent = (Collider *)gameObject->getComponent(ComponentType::COLLIDER);
//         if (colliderComponent)
//         {
//             physicsManager->addColliderComponent(colliderComponent);
//         }

//         Rigidbody *rigidBodyComponent = (Rigidbody *)gameObject->getComponent(ComponentType::RIGID_BODY);
//         if (rigidBodyComponent)
//         {
//             physicsManager->addRigidBodyComponent(rigidBodyComponent);
//         }
//     }

//     return gameObject;
// }

// void GameObjectManager::removeGameObject(GameObject *gameObject)
// {
//     gameObjects.remove(gameObject);

//     deleteGameObject(gameObject);
// }

// void GameObjectManager::deleteGameObject(GameObject *gameObject)
// {
//     if (physicsManager)
//     {
//         Collider *colliderComponent = (Collider *)gameObject->getComponent(ComponentType::COLLIDER);
//         if (colliderComponent)
//         {
//             physicsManager->removeColliderComponent(colliderComponent);
//         }

//         Rigidbody *rigidBodyComponent = (Rigidbody *)gameObject->getComponent(ComponentType::RIGID_BODY);
//         if (rigidBodyComponent)
//         {
//             physicsManager->removeRigidBodyComponent(rigidBodyComponent);
//         }
//     }

//     delete gameObject;
// }

// bool GameObjectManager::removeGameObject(std::string name)
// {
//     for (std::list<GameObject *>::iterator it = gameObjects.begin(); it != gameObjects.end(); ++it)
//     {
//         if ((*it)->getName() == name)
//         {
//             GameObject *gameObject = *it;
//             gameObjects.erase(it);
//             deleteGameObject(gameObject);
//             return true;
//         }
//     }

//     return false;
// }

// GameObject *GameObjectManager::getGameObject(std::string name)
// {
//     for (std::list<GameObject *>::iterator it = gameObjects.begin(); it != gameObjects.end(); ++it)
//     {
//         if ((*it)->getName() == name)
//         {
//             return *it;
//         }
//     }

//     return nullptr;
// }

// std::list<GameObject *> &GameObjectManager::getGameObjects()
// {
//     return gameObjects;
// }

// void GameObjectManager::setPhysicsManager(PhysicsManager *physicsManager)
// {
//     this->physicsManager = physicsManager;
// }

// PhysicsManager *GameObjectManager::getPhysicsManager() const
// {
//     return physicsManager;
// }

// void GameObjectManager::applyPhysics()
// {
//     if (physicsManager)
//     {
//         physicsManager->applyPhysics();
//     }
// }

// void GameObjectManager::resolveCollisions()
// {
//     if (physicsManager)
//     {
//         physicsManager->checkForCollisions();
//         physicsManager->resolveCollisions();
//     }
// }