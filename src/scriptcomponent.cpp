#include "platformator/scriptcomponent.h"

#include "platformator/collider.h"
#include "gamemanager.h"
#include "runtimeaccess.h"
#include "platformator/runtime.h"
#include <cstdio>

ScriptComponent::ScriptComponent() : Component(ComponentType::SCRIPT), behaviors(), gameManagerIndex(SIZE_MAX)
{
}

ScriptComponent::ScriptComponent(GameObject *gameObject)
    : Component(gameObject, ComponentType::SCRIPT), behaviors(), gameManagerIndex(SIZE_MAX)
{
}

ScriptComponent::~ScriptComponent()
{
    for (auto *behavior : behaviors)
        delete behavior;
    for (auto *behavior : pendingBehaviors)
        delete behavior;
}

const std::vector<Behavior *> &ScriptComponent::getBehaviors() const
{
    return behaviors;
}

Behavior *ScriptComponent::addBehavior(Behavior *behavior)
{
    auto *runtime = platformator::Runtime::tryGetCurrent();
    if (runtime && getLifecycle() != ObjectLifecycle::Staged)
        platformator_detail::RuntimeAccess::gameManager().assertCanMutate();
    if (!behavior || isRetired())
        throw std::invalid_argument("Cannot attach behavior to retired script, or attach null");
    if (behavior->scriptComponent)
        throw std::invalid_argument("Behavior already has an owner");
    pendingBehaviors.push_back(behavior);
    try
    {
        if (gameManagerIndex != SIZE_MAX && getIsCallbackEligible())
            platformator_detail::RuntimeAccess::gameManager().queueScriptStart(this);
    }
    catch (...)
    {
        pendingBehaviors.pop_back();
        throw;
    }
    behavior->scriptComponent = this;
    behavior->setGameObject(getGameObject());
    return behavior;
}

void ScriptComponent::startPendingBehaviors(platformator::ReferenceResolver &resolver)
{
    size_t processed = 0;
    for (; canDispatch() && processed < pendingBehaviors.size(); ++processed)
    {
        auto *behavior = pendingBehaviors[processed];
        try
        {
            behavior->resolveReferences(resolver);
            behavior->start();
        }
        catch (const std::exception &error)
        {
            std::fprintf(stderr, "Behavior '%s' on '%s' initialization failed: %s\n", behavior->getTypeName().c_str(),
                         getGameObject()->getName().c_str(), error.what());
        }
        catch (...)
        {
            std::fprintf(stderr, "Behavior '%s' on '%s' initialization failed: unknown exception\n",
                         behavior->getTypeName().c_str(), getGameObject()->getName().c_str());
        }
    }
    behaviors.insert(behaviors.end(), pendingBehaviors.begin(), pendingBehaviors.begin() + processed);
    pendingBehaviors.erase(pendingBehaviors.begin(), pendingBehaviors.begin() + processed);
}

void ScriptComponent::update(double timeDelta)
{
    const size_t count = behaviors.size();
    for (size_t i = 0; i < count && canDispatch(); ++i)
        behaviors[i]->update(timeDelta);
}

void ScriptComponent::fixedUpdate(double timeDelta)
{
    const size_t count = behaviors.size();
    for (size_t i = 0; i < count && canDispatch(); ++i)
        behaviors[i]->fixedUpdate(timeDelta);
}

void ScriptComponent::lateUpdate(double timeDelta)
{
    const size_t count = behaviors.size();
    for (size_t i = 0; i < count && canDispatch(); ++i)
        behaviors[i]->lateUpdate(timeDelta);
}

void ScriptComponent::setGameObject(GameObject *gameObject)
{
    Component::setGameObject(gameObject);

    for (auto *behavior : behaviors)
        behavior->setGameObject(gameObject);
    for (auto *behavior : pendingBehaviors)
        behavior->setGameObject(gameObject);
}

void ScriptComponent::dispatchCollisionEnter(const Collision *collision, Collider *other, double timeDelta)
{
    const size_t count = behaviors.size();
    for (size_t i = 0; i < count && canDispatch(); ++i)
        behaviors[i]->onCollisionEnter(collision, other, timeDelta);
}

void ScriptComponent::dispatchCollisionExit(Collider *other, double timeDelta)
{
    const size_t count = behaviors.size();
    for (size_t i = 0; i < count && canDispatch(); ++i)
        behaviors[i]->onCollisionExit(other, timeDelta);
}

size_t ScriptComponent::getGameManagerIndex() const
{
    return gameManagerIndex;
}

void ScriptComponent::setGameManagerIndex(size_t index)
{
    gameManagerIndex = index;
}

bool ScriptComponent::canDispatch() const
{
    const auto *owner = getGameObject();
    return getIsCallbackEligible() && !isRetired() && owner->getActive() && !owner->isRetired() &&
           owner->getComponent<ScriptComponent>() == this;
}
