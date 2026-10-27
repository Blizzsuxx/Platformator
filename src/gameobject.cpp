#include "platformator/gameobject.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "platformator/collider.h"
#include "gamemanager.h"
#include "platformator/rigidbody.h"
#include "runtimeaccess.h"
#include "platformator/runtime.h"
#include "inputvalidation.h"

GameObject::GameObject()
    : rotation(0.0f), sinRotation(0.0f), cosRotation(1.0f), parent(nullptr), position(Eigen::Vector2f::Zero()),
      scale(Eigen::Vector2f::Ones()), name(""), tag(""), components(), children(), gameManagerIteratorIndex(0),
      flags(static_cast<uint8_t>(IS_ACTIVE))
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
    return rotation * 180.0f / static_cast<float>(M_PI);
}

bool GameObject::getActive() const
{
    return flags & IS_ACTIVE;
}

Eigen::Vector2f GameObject::getPosition() const
{
    if (parent != nullptr)
    {
        return position + parent->getPosition();
    }
    return position;
}

float GameObject::getX() const
{
    return getPosition().x();
}

float GameObject::getY() const
{
    return getPosition().y();
}

const Eigen::Vector2f &GameObject::getScale() const
{
    return scale;
}

const std::string &GameObject::getName() const
{
    return name;
}

const std::string &GameObject::getTag() const
{
    return tag;
}

GameObject *GameObject::getParent() const
{
    return parent;
}

Component *GameObject::getComponent(const ComponentType &componentType) const
{
    return components[componentType];
}

Component *const *GameObject::getComponents() const
{
    return components;
}

const std::vector<GameObject *> &GameObject::getChildren() const
{
    return children;
}

float GameObject::getCosRotation() const
{
    return cosRotation;
}

float GameObject::getSinRotation() const
{
    return sinRotation;
}

// Setters
GameObject *GameObject::setRotation(const float rotation)
{
    validateMutation();
    platformator_detail::finite(rotation, "rotation");
    const float wrapped = std::fmod(rotation, 2.0f * static_cast<float>(M_PI));
    if (this->rotation == wrapped)
        return this;
    this->rotation = wrapped;
    this->sinRotation = std::sin(this->rotation);
    this->cosRotation = std::cos(this->rotation);
    if (auto *body = getComponent<Rigidbody>())
        body->wakeUp();

    updateCollider();
    return this;
}

GameObject *GameObject::setActive(const bool active)
{
    validateMutation();
    if (getActive() == active)
        return this;
    if (active)
        flags |= IS_ACTIVE;
    else
        flags &= ~IS_ACTIVE;
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
    if (!position.allFinite())
        throw std::invalid_argument("position must be finite");
    const auto local = parent ? (position - parent->getPosition()).eval() : position;
    if (this->position == local)
        return this;
    this->position = local;

    queueTransform();
    if (auto *body = getComponent<Rigidbody>())
        body->wakeUp();

    return this;
}

void GameObject::queueTransform()
{
    if (getIsRegisteredInGameManager())
        platformator_detail::RuntimeAccess::gameManager().queueTransform(this);
    else
        worldPosition = getPosition();
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
    this->name = name;
    return this;
}

GameObject *GameObject::setTag(const std::string &tag)
{
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
    if (getIsRegisteredInGameManager())
        platformator_detail::RuntimeAccess::gameManager().assignFileId(*component);
    addComponentInternal(component);
    if (getIsRegisteredInGameManager())
    {
        auto &manager = platformator_detail::RuntimeAccess::gameManager();
        manager.notifyComponentAdded(component);
    }
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

void GameObject::addComponentInternal(Component *component)
{
    if (component == nullptr)
    {
        throw std::invalid_argument("Component cannot be null");
    }

    ComponentType type = component->getType();
    if (components[type] != nullptr)
    {
        auto *old = components[type];
        if (getIsRegisteredInGameManager())
            platformator_detail::RuntimeAccess::gameManager().retireComponent(old);
        else
            delete old;
    }

    components[type] = component;
    component->setGameObject(this);

    if (type == ComponentType::COLLIDER || type == ComponentType::RIGID_BODY)
    {
        Rigidbody *rigidbodyComponent = getComponent<Rigidbody>();
        if (rigidbodyComponent != nullptr)
        {
            rigidbodyComponent->refreshMomentOfInertiaCache();
        }
    }

    if (component->getType() == ComponentType::COLLIDER)
    {
        static_cast<Collider *>(component)->scheduleSync();
    }
    if (type == RIGID_BODY)
        if (auto *collider = getComponent<Collider>())
            collider->invalidateResponse();
}

bool GameObject::removeComponent(const ComponentType &componentType)
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
    if (componentType == RIGID_BODY)
        if (auto *collider = getComponent<Collider>())
            collider->invalidateResponse();
    if (getIsRegisteredInGameManager())
        platformator_detail::RuntimeAccess::gameManager().flushIfIdle();
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
    child->parentIndex = children.size();
    children.push_back(child);
    child->parent = this;
    child->position = world - getPosition();
    if (getIsRegisteredInGameManager())
        platformator_detail::RuntimeAccess::gameManager().hierarchyDirty = true;
    child->queueTransform();
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
    child->queueTransform();
    return true;
}

bool GameObject::getIsMarkedForDeletion() const
{
    return flags & IS_MARKED_FOR_DELETION;
}

GameObject *GameObject::setIsMarkedForDeletion(const bool markedForDeletion)
{
    if (markedForDeletion)
    {
        flags |= IS_MARKED_FOR_DELETION;
    }
    else
    {
        flags &= ~IS_MARKED_FOR_DELETION;
    }
    return this;
}

GameObject *GameObject::setIsRegisteredInGameManager(const bool isRegisteredInGameManager)
{
    if (isRegisteredInGameManager)
    {
        flags |= IS_REGISTERED_IN_GAME_MANAGER;
    }
    else
    {
        flags &= ~IS_REGISTERED_IN_GAME_MANAGER;
    }

    return this;
}

GameObject *GameObject::setGameManagerIteratorIndex(const size_t index)
{
    gameManagerIteratorIndex = index;
    return this;
}

bool GameObject::getIsRegisteredInGameManager() const
{
    return flags & IS_REGISTERED_IN_GAME_MANAGER;
}

void GameObject::addComponentsToGameManager()
{
    for (Component *component : components)
    {
        if (component != nullptr)
        {
            platformator_detail::RuntimeAccess::gameManager().notifyComponentAdded(component);
        }
    }
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
    auto *moved = parent->children.back();
    parent->children[parentIndex] = moved;
    moved->parentIndex = parentIndex;
    parent->children.pop_back();
    parent = nullptr;
    parentIndex = SIZE_MAX;
    position = world;
    if (getIsRegisteredInGameManager())
        platformator_detail::RuntimeAccess::gameManager().hierarchyDirty = true;
}
