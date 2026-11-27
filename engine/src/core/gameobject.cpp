#include "platformator/gameobject.h"

#include <cmath>
#include <numbers>
#include <stdexcept>

#include "benchmark/workprobe.h"
#include "core/gamemanager.h"
#include "core/inputvalidation.h"
#include "core/membership.h"
#include "core/objectpool.h"
#include "core/runtimeaccess.h"
#include "platformator/collider.h"
#include "platformator/rigidbody.h"

Component::Component(GameObject *gameObject, ComponentType type) : gameObject(gameObject), type(type)
{
}

Component::Component(ComponentType type) : gameObject(nullptr), type(type)
{
}

void Component::setGameObject(GameObject *gameObject)
{
    this->gameObject = gameObject;
}

PLATFORMATOR_DEFINE_POOLED_ALLOCATORS(GameObject, GameObject)

GameObject::GameObject()
    : rotation(0.0f), sinRotation(0.0f), cosRotation(1.0f), parent(nullptr), position(Eigen::Vector2f::Zero()),
      scale(Eigen::Vector2f::Ones()), name(), tag(), components(), children(), gameManagerIteratorIndex(SIZE_MAX),
      active(true)
{
}

GameObject::~GameObject()
{
    for (auto *component : components)
        delete component;
}

// Getters
float GameObject::getRotation() const
{
    return rotation;
}

float GameObject::getRotationInDegrees() const
{
    return rotation * 180.0f / std::numbers::pi_v<float>;
}

Eigen::Vector2f GameObject::getPosition() const
{
    PLATFORMATOR_WORK_ADD(positionReadVisits, 1);
    return immediateWorldPosition;
}

float GameObject::getX() const
{
    return getPosition().x();
}

float GameObject::getY() const
{
    return getPosition().y();
}

// Setters
GameObject *GameObject::setRotation(const float rotation)
{
    validateMutation();
    platformator_detail::finite(rotation, "rotation");
    const float wrapped = std::fmod(rotation, 2.0f * std::numbers::pi_v<float>);
    if (this->rotation == wrapped)
        return this;
    this->rotation = wrapped;
    this->sinRotation = std::sin(this->rotation);
    this->cosRotation = std::cos(this->rotation);
    if (getIsRegisteredInGameManager())
        platformator_detail::RuntimeAccess::gameManager().publishRenderGeometryChange(this);
    if (auto *body = getComponent<Rigidbody>())
        body->wakeUp();

    updateCollider();
    return this;
}

GameObject *GameObject::setActive(const bool active)
{
    validateMutation();
    if (this->active == active)
        return this;
    this->active = active;
    if (getIsRegisteredInGameManager())
    {
        auto &manager = platformator_detail::RuntimeAccess::gameManager();
        for (auto *component : components)
            if (component)
                manager.queueReconciliation(component);
        manager.flushIfIdle();
    }
    return this;
}

GameObject *GameObject::setPosition(const Eigen::Vector2f &position)
{
    validateMutation();
    platformator_detail::finite(position, "position");
    const auto local = parent ? (position - parent->getPosition()).eval() : position;
    return applyLocalPosition(local);
}

GameObject *GameObject::setLocalPosition(const Eigen::Vector2f &localPosition)
{
    validateMutation();
    platformator_detail::finite(localPosition, "position");
    return applyLocalPosition(localPosition);
}

GameObject *GameObject::applyLocalPosition(const Eigen::Vector2f &localPosition)
{
    if (this->position == localPosition)
        return this;
    this->position = localPosition;

    queueTransform();
    if (auto *body = getComponent<Rigidbody>())
        body->wakeUp();

    return this;
}

void GameObject::queueTransform(bool refreshImmediate)
{
    if (getIsRegisteredInGameManager())
        platformator_detail::RuntimeAccess::gameManager().queueTransform(this, refreshImmediate);
    else
    {
        if (refreshImmediate)
            refreshImmediatePositions();
        auto syncPublished = [](auto &&self, GameObject *node) -> void {
            node->worldPosition = node->immediateWorldPosition;
            for (auto *child : node->children)
                self(self, child);
        };
        syncPublished(syncPublished, this);
    }
}

void GameObject::refreshImmediatePositions()
{
    // EFFICIENCY: Every public ancestor edit visits O(subtree) objects. Eager writes keep
    // immediate reads stable for collision readers; if edit-heavy workloads regress, replace
    // this walk with main-thread batched subtree publication and an explicit reader boundary.
    PLATFORMATOR_WORK_ADD(immediatePositionRefreshVisits, 1);
    immediateWorldPosition = parent ? (position + parent->immediateWorldPosition).eval() : position;
    for (auto *child : children)
        child->refreshImmediatePositions();
}

void GameObject::updateCollider()
{
    Component *colliderComponent = components[ComponentType::COLLIDER];
    if (colliderComponent != nullptr)
    {
        static_cast<Collider *>(colliderComponent)->scheduleSync();
    }
}

GameObject *GameObject::setScale(const Eigen::Vector2f &scale)
{
    validateMutation();
    platformator_detail::positive(scale.x(), "scale.x");
    platformator_detail::positive(scale.y(), "scale.y");
    if (this->scale == scale)
        return this;
    this->scale = scale;
    if (getIsRegisteredInGameManager())
        platformator_detail::RuntimeAccess::gameManager().publishRenderGeometryChange(this);

    Rigidbody *rigidbodyComponent = getComponent<Rigidbody>();
    if (rigidbodyComponent != nullptr)
    {
        rigidbodyComponent->refreshMomentOfInertiaCache();
        rigidbodyComponent->wakeUp();
    }

    updateCollider();
    return this;
}

GameObject *GameObject::setName(const std::string &name)
{
    validateMutation();
    this->name = name;
    return this;
}

GameObject *GameObject::setTag(const std::string &tag)
{
    validateMutation();
    this->tag = tag;
    return this;
}

void GameObject::addComponent(Component *component)
{
    attachComponent(component);
    flushMutations();
}

void GameObject::attachComponent(Component *component)
{
    validateMutation();
    if (!component)
        throw std::invalid_argument("Component cannot be null");
    if (component->getHandle())
        throw std::invalid_argument("Component already belongs to a runtime/build");
    const ComponentType type = component->getType();
    if (type < 0 || type >= COMPONENT_TYPE_COUNT)
        throw std::invalid_argument("Unknown component type");
    auto *old = components[type];
    if (auto *owner = component->getGameObject(); owner && owner->components[type] == component)
        throw std::invalid_argument("Component is already attached");
    auto *manager = getIsRegisteredInGameManager() ? &platformator_detail::RuntimeAccess::gameManager() : nullptr;
    if (manager)
        manager->prepareComponentAttachment(*this, *component, old);
    else
        component->setGameObject(this);

    if (old)
    {
        if (manager)
            manager->retireComponent(old);
        else
            delete old;
    }

    components[type] = component;

    if (type == ComponentType::COLLIDER || type == ComponentType::RIGID_BODY)
    {
        Rigidbody *rigidbodyComponent = getComponent<Rigidbody>();
        if (auto *collider = getComponent<Collider>())
        {
            collider->attachedRigidbody = rigidbodyComponent;
        }
        if (rigidbodyComponent != nullptr)
        {
            rigidbodyComponent->refreshMomentOfInertiaCache();
        }
    }

    if (type == ComponentType::COLLIDER)
    {
        static_cast<Collider *>(component)->scheduleSync();
    }
    if (type == ComponentType::RIGID_BODY)
        if (auto *collider = getComponent<Collider>())
            collider->invalidateResponse();
    if (manager)
        manager->notifyComponentAdded(component);
}

void GameObject::flushMutations()
{
    if (getIsRegisteredInGameManager())
        platformator_detail::RuntimeAccess::gameManager().flushIfIdle();
}

Component *GameObject::getColliderByShape(bool box) const
{
    auto *collider = static_cast<Collider *>(components[COLLIDER]);
    return collider && (collider->getColliderType() == ColliderType::BoxCollider) == box ? collider : nullptr;
}

bool GameObject::removeComponent(ComponentType componentType)
{
    validateMutation();
    auto *component = components[componentType];
    if (!component)
        return false;
    components[componentType] = nullptr;
    if (getIsRegisteredInGameManager())
        platformator_detail::RuntimeAccess::gameManager().retireComponent(component);
    else
        delete component;
    if (componentType == ComponentType::COLLIDER)
        if (auto *body = getComponent<Rigidbody>())
            body->refreshMomentOfInertiaCache();
    if (componentType == ComponentType::RIGID_BODY)
        if (auto *collider = getComponent<Collider>())
        {
            collider->attachedRigidbody = nullptr;
            collider->invalidateResponse();
        }
    flushMutations();
    return true;
}

void GameObject::addChild(GameObject *child)
{
    validateMutation();
    if (!child)
        throw std::invalid_argument("Child cannot be null");
    child->validateMutation();
    for (auto *ancestor = this; ancestor; ancestor = ancestor->parent)
        if (ancestor == child)
            throw std::invalid_argument("Parenting would create a cycle");
    if (getIsRegisteredInGameManager() != child->getIsRegisteredInGameManager() ||
        getHandle().runtimeSerial != child->getHandle().runtimeSerial)
        throw std::invalid_argument("Parent and child must belong to the same runtime/build");
    if (child->parent == this)
        return;
    const auto world = child->getPosition().eval();
    child->detachFromParent();
    children.push_back(child);
    child->parentIndex = children.size() - 1;
    child->parent = this;
    child->position = world - getPosition();
    child->queueTransform(false);
}

bool GameObject::removeChild(GameObject *child)
{
    validateMutation();
    if (!child)
        throw std::invalid_argument("Child cannot be null");
    child->validateMutation();
    if (child->parent != this)
        return false;
    child->detachFromParent();
    child->queueTransform(false);
    return true;
}

void GameObject::destroy()
{
    platformator_detail::RuntimeAccess::gameManager().destroyGameObject(this);
}

void GameObject::validateMutation() const
{
    if (getLifecycle() != ObjectLifecycle::Staged)
        platformator_detail::RuntimeAccess::gameManager().assertCanMutate();
    if (isRetired())
        throw std::logic_error("Cannot structurally mutate a retired GameObject");
}

void GameObject::detachFromParent()
{
    if (!parent)
        return;
    const auto world = getPosition().eval();
    membership(parent->children, this, &GameObject::parentIndex, false);
    parent = nullptr;
    position = world;
}
