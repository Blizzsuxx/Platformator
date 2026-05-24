#include "scriptcomponent.h"

#include "collider.h"
#include "gamemanager.h"
#include "runtimeaccess.h"

ScriptComponent::ScriptComponent() : Component(ComponentType::SCRIPT), behaviors(), gameManagerIndex(SIZE_MAX)
{
}

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
    if (gameManagerIndex != SIZE_MAX && getGameObject() != nullptr && getGameObject()->getActive() &&
        !getGameObject()->getIsMarkedForDeletion())
    {
        platformator_detail::RuntimeAccess::gameManager().addStartedBehavior(behavior);
    }

    return behavior;
}

void ScriptComponent::update(double timeDelta)
{
    const size_t behaviorCount = behaviors.size();
    for (size_t index = 0; index < behaviorCount; ++index)
    {
        Behavior *behavior = behaviors[index];

        behavior->update(timeDelta);
    }
}

void ScriptComponent::fixedUpdate(double timeDelta)
{
    const size_t behaviorCount = behaviors.size();
    for (size_t index = 0; index < behaviorCount; ++index)
    {
        Behavior *behavior = behaviors[index];

        behavior->fixedUpdate(timeDelta);
    }
}

void ScriptComponent::lateUpdate(double timeDelta)
{
    const size_t behaviorCount = behaviors.size();
    for (size_t index = 0; index < behaviorCount; ++index)
    {
        Behavior *behavior = behaviors[index];

        behavior->lateUpdate(timeDelta);
    }
}

void ScriptComponent::setGameObject(GameObject *gameObject)
{
    Component::setGameObject(gameObject);

    for (Behavior *behavior : behaviors)
    {
        if (behavior != nullptr)
        {
            behavior->setGameObject(gameObject);
        }
    }
}

void ScriptComponent::destroyBehaviors()
{
    const size_t behaviorCount = behaviors.size();
    for (size_t index = 0; index < behaviorCount; ++index)
    {
        Behavior *behavior = behaviors[index];
        platformator_detail::RuntimeAccess::gameManager().removeStartedBehavior(behavior);
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

        behavior->onCollisionEnter(collision, other, timeDelta);
    }
}

void ScriptComponent::dispatchCollisionExit(Collider *other, double timeDelta)
{
    const size_t behaviorCount = behaviors.size();
    for (size_t index = 0; index < behaviorCount; ++index)
    {
        Behavior *behavior = behaviors[index];
        behavior->onCollisionExit(other, timeDelta);
    }
}

void ScriptComponent::dispatchCollisionStay(const Collision *collision, Collider *other, double timeDelta)
{
    const size_t behaviorCount = behaviors.size();
    for (size_t index = 0; index < behaviorCount; ++index)
    {
        Behavior *behavior = behaviors[index];

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