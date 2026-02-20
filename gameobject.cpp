#include "gameobject.h"
#include "collider.h"

GameObject::GameObject() : rotation(0.0f), sinRotation(0.0f), cosRotation(1.0f), isActive(true), position(Eigen::Vector2f::Zero()), scale(Eigen::Vector2f::Ones()), name(""), tag(""), components()
{
}

GameObject::GameObject(const float rotation, const bool active, const Eigen::Vector2f &position, const Eigen::Vector2f &scale, const std::string &name, const std::string &tag)
    : rotation(rotation), sinRotation(std::sin(rotation)), cosRotation(std::cos(rotation)), isActive(active), position(position), scale(scale), name(name), tag(tag), components()
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
    return isActive;
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

const std::list<GameObject *> &GameObject::getChildren() const
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
void GameObject::setRotation(const float rotation)
{
    this->rotation = rotation;
    this->sinRotation = std::sin(rotation);
    this->cosRotation = std::cos(rotation);

    updateCollider();
}

void GameObject::setActive(const bool active)
{
    this->isActive = active;
}

void GameObject::setPosition(const Eigen::Vector2f &position)
{
    this->position = position;

    updateCollider();
}

void GameObject::updateCollider()
{
    Component *colliderComponent = components[ComponentType::COLLIDER];
    if (colliderComponent != nullptr)
    {
        static_cast<Collider *>(colliderComponent)->updateCollider();
    }
}

void GameObject::setScale(const Eigen::Vector2f &scale)
{
    this->scale = scale;

    updateCollider();
}

void GameObject::setName(const std::string &name)
{
    this->name = name;
}

void GameObject::setTag(const std::string &tag)
{
    this->tag = tag;
}

bool GameObject::addComponent(Component *component)
{
    if (component == nullptr)
    {
        return false;
    }

    ComponentType type = component->getType();
    if (components[type] != nullptr)
    {
        return false;
    }

    components[type] = component;
    return true;
}

bool GameObject::removeComponent(const ComponentType &componentType)
{
    if (components[componentType] == nullptr)
    {
        return false;
    }

    delete components[componentType];
    components[componentType] = nullptr;

    return true;
}

bool GameObject::addChild(GameObject *child)
{
    if (child == nullptr)
    {
        return false;
    }

    children.push_back(child);
    return true;
}

bool GameObject::removeChild(GameObject *child)
{
    if (child == nullptr)
    {
        return false;
    }

    children.remove(child);
    delete child;

    return true;
}
