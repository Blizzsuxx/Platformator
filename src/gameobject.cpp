#include "gameobject.h"
#include "collider.h"
#include "gamemanager.h"
#include "rigidbody.h"

GameObject::GameObject() : rotation(0.0f), sinRotation(0.0f), cosRotation(1.0f), position(Eigen::Vector2f::Zero()), scale(Eigen::Vector2f::Ones()), name(""), tag(""), components(), children(), gameManagerIteratorIndex(0), flags(static_cast<uint8_t>(IS_ACTIVE))
{
}

GameObject::GameObject(const float rotation, const bool active, const Eigen::Vector2f &position, const Eigen::Vector2f &scale, const std::string &name, const std::string &tag)
    : rotation(rotation), sinRotation(std::sin(rotation)), cosRotation(std::cos(rotation)), position(position), scale(scale), name(name), tag(tag), components(), children(), gameManagerIteratorIndex(0), flags(static_cast<uint8_t>(active ? IS_ACTIVE : GAME_OBJECT_NONE))
{
}

GameObject::~GameObject()
{
    for (Component *component : components)
    {
        if (component != nullptr)
        {
            delete component;
        }
    }

    for (GameObject *child : children)
    {
        if (child != nullptr)
        {
            delete child;
        }
    }
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

const Eigen::Vector2f &GameObject::getPosition() const
{
    return position;
}

float GameObject::getX() const
{
    return position.x();
}

float GameObject::getY() const
{
    return position.y();
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

Component *GameObject::getComponent(const ComponentType &componentType) const
{
    return components[componentType];
}

Component **GameObject::getComponents()
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
    this->rotation = std::fmod(rotation, 2.0f * static_cast<float>(M_PI));
    this->sinRotation = std::sin(this->rotation);
    this->cosRotation = std::cos(this->rotation);

    updateCollider();
    return this;
}

GameObject *GameObject::setActive(const bool active)
{
    if (this->getActive() == active)
    {
        return this;
    }

    if (active)
    {
        flags |= IS_ACTIVE;
    }
    else
    {
        flags &= ~IS_ACTIVE;
    }

    GameManager *gameManager = &GameManager::getInstance();
    ComponentType managedComponents[] = {ComponentType::COLLIDER, ComponentType::RIGID_BODY, ComponentType::SPRITE, ComponentType::CAMERA};

    if (active)
    {
        addComponentsToGameManager();
    }
    else
    {
        removeComponentsFromGameManager();
    }

    return this;
}

GameObject *GameObject::setPosition(const Eigen::Vector2f &position)
{
    this->position = position;

    updateCollider();
    return this;
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
    this->scale = scale;

    Rigidbody *rigidbodyComponent = getComponent<Rigidbody>();
    if (rigidbodyComponent != nullptr)
    {
        rigidbodyComponent->refreshMomentOfInertiaCache();
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
    addComponentInternal(component);

    GameManager::getInstance().notifyComponentAdded(component);
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
        removeComponent(type);
    }

    components[type] = component;

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
}

bool GameObject::removeComponent(const ComponentType &componentType)
{
    if (components[componentType] == nullptr)
    {
        return false;
    }

    GameManager::getInstance().notifyComponentRemoved(components[componentType]);
    delete components[componentType];
    components[componentType] = nullptr;

    if (componentType == ComponentType::COLLIDER)
    {
        Rigidbody *rigidbodyComponent = getComponent<Rigidbody>();
        if (rigidbodyComponent != nullptr)
        {
            rigidbodyComponent->refreshMomentOfInertiaCache();
        }
    }

    return true;
}

void GameObject::addChild(GameObject *child)
{
    if (child == nullptr)
    {
        throw std::invalid_argument("Child cannot be null");
    }

    children.push_back(child);
}

bool GameObject::removeChild(GameObject *child)
{
    if (child == nullptr)
    {
        throw std::invalid_argument("Child cannot be null");
    }

    auto it = std::find(children.begin(), children.end(), child);
    if (it != children.end())
    {
        children.erase(it);
        return true;
    }

    return false;
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
            GameManager::getInstance().notifyComponentAdded(component);
        }
    }
}

void GameObject::removeComponentsFromGameManager()
{
    for (Component *component : components)
    {
        if (component != nullptr)
        {
            GameManager::getInstance().notifyComponentRemoved(component);
        }
    }
}