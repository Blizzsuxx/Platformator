#include "platformator/scriptcomponent.h"

#include "platformator/collider.h"
#include "gamemanager.h"
#include "runtimeaccess.h"
#include "platformator/runtime.h"

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
    behaviors.push_back(behavior);
    behavior->scriptComponent = this;
    behavior->setGameObject(getGameObject());
    if (gameManagerIndex != SIZE_MAX && getIsCallbackEligible())
    {
        platformator_detail::RuntimeAccess::gameManager().addStartedBehavior(behavior);
        if (auto *collider = getGameObject()->getComponent<Collider>())
            platformator_detail::RuntimeAccess::gameManager().getPhysicsManager()->refreshSubscriptions(collider);
    }
    return behavior;
}

void ScriptComponent::update(double timeDelta)
{
    const size_t count = behaviors.size();
    for (size_t i = 0; i < count && canDispatch(); ++i)
        if (behaviors[i]->started)
            behaviors[i]->update(timeDelta);
}

void ScriptComponent::fixedUpdate(double timeDelta)
{
    const size_t count = behaviors.size();
    for (size_t i = 0; i < count && canDispatch(); ++i)
        if (behaviors[i]->started)
            behaviors[i]->fixedUpdate(timeDelta);
}

void ScriptComponent::lateUpdate(double timeDelta)
{
    const size_t count = behaviors.size();
    for (size_t i = 0; i < count && canDispatch(); ++i)
        if (behaviors[i]->started)
            behaviors[i]->lateUpdate(timeDelta);
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
    for (auto *behavior : behaviors)
        delete behavior;
    behaviors.clear();
}

void ScriptComponent::dispatchCollisionEnter(const Collision *collision, Collider *other, double timeDelta)
{
    const size_t count = behaviors.size();
    for (size_t i = 0; i < count && canDispatch(); ++i)
        if (behaviors[i]->started)
            behaviors[i]->onCollisionEnter(collision, other, timeDelta);
}

void ScriptComponent::dispatchCollisionExit(Collider *other, double timeDelta)
{
    const size_t count = behaviors.size();
    for (size_t i = 0; i < count && canDispatch(); ++i)
        if (behaviors[i]->started)
            behaviors[i]->onCollisionExit(other, timeDelta);
}

void ScriptComponent::dispatchCollisionStay(const Collision *collision, Collider *other, double timeDelta)
{
    const size_t count = behaviors.size();
    for (size_t i = 0; i < count && canDispatch(); ++i)
        if (behaviors[i]->started)
            behaviors[i]->onCollisionStay(collision, other, timeDelta);
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
