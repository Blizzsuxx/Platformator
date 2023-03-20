#pragma once

#include "gameobjectmanager.h"

class PhysicsManager
{
public:
    PhysicsManager();
    ~PhysicsManager();

    void applyPhysics(GameObjectManager& gameObjectManager);
    void resolveCollisions(GameObjectManager& gameObjectManager);
};