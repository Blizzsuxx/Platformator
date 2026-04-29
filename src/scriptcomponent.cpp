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

template <typename T>
T *ScriptComponent::getBehavior() const
{
    for (Behavior *behavior : behaviors)
    {
        if (T *castedBehavior = dynamic_cast<T *>(behavior))
        {
            return castedBehavior;
        }
    }
    return nullptr;
}

Behavior *ScriptComponent::addBehavior(Behavior *behavior)
{
    behaviors.push_back(behavior);
    behavior->setGameObject(getGameObject());
    if (gameManagerIndex != SIZE_MAX && getGameObject() != nullptr && getGameObject()->getActive() &&
        !getGameObject()->getIsMarkedForDeletion())
    {
        GameManager::getInstance().addStartedBehavior(behavior);
    }

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
        GameManager::getInstance().removeStartedBehavior(behavior);
        behavior->onDestroy();

        delete behavior;
    }

    behaviors.clear();
}

void ScriptComponent::dispatchCollisionEnter(const Collision *collision, Collider *other, double timeDelta)
{
    const size_t behaviorCount = behaviors.size();
    for (size_t index = 0; index < behaviorCount; ++index)
    {
        Behavior *behavior = behaviors[index];
        if (!behavior->getEnabled())
        {
            continue;
        }

        behavior->onCollisionEnter(collision, other, timeDelta);
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

void ScriptComponent::dispatchCollisionStay(const Collision *collision, Collider *other, double timeDelta)
{
    const size_t behaviorCount = behaviors.size();
    for (size_t index = 0; index < behaviorCount; ++index)
    {
        Behavior *behavior = behaviors[index];
        if (!behavior->getEnabled())
        {
            continue;
        }

        behavior->onCollisionStay(collision, other, timeDelta);
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