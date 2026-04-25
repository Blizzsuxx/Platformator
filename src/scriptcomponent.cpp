#include "scriptcomponent.h"

#include "collider.h"
#include "gamemanager.h"

ScriptComponent::ScriptComponent(GameObject *gameObject)
    : Component(gameObject, ComponentType::SCRIPT), behaviors(), gameManagerIndex(SIZE_MAX)
{
}

ScriptComponent::~ScriptComponent()
{
    destroyBehaviors();
}

const std::vector<Behavior *> &ScriptComponent::getBehaviors() const
{
    return behaviors;
}

Behavior *ScriptComponent::addBehavior(Behavior *behavior)
{
    behaviors.push_back(behavior);
    behavior->setGameObject(getGameObject());
    GameManager::getInstance().addStartedBehavior(behavior);

    return behavior;
}

void ScriptComponent::update(double timeDelta)
{
    const size_t behaviorCount = behaviors.size();
    for (size_t index = 0; index < behaviorCount; ++index)
    {
        Behavior *behavior = behaviors[index];
        if (!behavior->getEnabled())
        {
            continue;
        }

        behavior->update(timeDelta);
    }
}

void ScriptComponent::fixedUpdate(double timeDelta)
{
    const size_t behaviorCount = behaviors.size();
    for (size_t index = 0; index < behaviorCount; ++index)
    {
        Behavior *behavior = behaviors[index];
        if (!behavior->getEnabled())
        {
            continue;
        }

        behavior->fixedUpdate(timeDelta);
    }
}

void ScriptComponent::lateUpdate(double timeDelta)
{
    const size_t behaviorCount = behaviors.size();
    for (size_t index = 0; index < behaviorCount; ++index)
    {
        Behavior *behavior = behaviors[index];
        if (!behavior->getEnabled())
        {
            continue;
        }

        behavior->lateUpdate(timeDelta);
    }
}

void ScriptComponent::destroyBehaviors()
{
    const size_t behaviorCount = behaviors.size();
    for (size_t index = 0; index < behaviorCount; ++index)
    {
        Behavior *behavior = behaviors[index];
        behavior->onDestroy();

        delete behavior;
    }

    behaviors.clear();
}

void ScriptComponent::dispatchCollisionEnter(Collider *other, double timeDelta)
{
    const size_t behaviorCount = behaviors.size();
    for (size_t index = 0; index < behaviorCount; ++index)
    {
        Behavior *behavior = behaviors[index];
        if (!behavior->getEnabled())
        {
            continue;
        }

        behavior->onCollisionEnter(other, timeDelta);
    }
}

void ScriptComponent::dispatchCollisionExit(Collider *other, double timeDelta)
{
    const size_t behaviorCount = behaviors.size();
    for (size_t index = 0; index < behaviorCount; ++index)
    {
        Behavior *behavior = behaviors[index];
        if (!behavior->getEnabled())
        {
            continue;
        }

        behavior->onCollisionExit(other, timeDelta);
    }
}

void ScriptComponent::dispatchCollisionStay(Collider *other, double timeDelta)
{
    const size_t behaviorCount = behaviors.size();
    for (size_t index = 0; index < behaviorCount; ++index)
    {
        Behavior *behavior = behaviors[index];
        if (!behavior->getEnabled())
        {
            continue;
        }

        behavior->onCollisionStay(other, timeDelta);
    }
}

size_t ScriptComponent::getGameManagerIndex() const
{
    return gameManagerIndex;
}

void ScriptComponent::setGameManagerIndex(size_t index)
{
    gameManagerIndex = index;
}