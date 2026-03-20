#pragma once

#include <Eigen/Dense>
#include <list>
#include <memory>
#include <type_traits>
#include <utility>

class GameObject;

enum ComponentType
{
    ANIMATOR = 0,
    AUDIO,
    CAMERA,
    COLLIDER,
    LIGHT,
    RIGID_BODY,
    SPRITE,
    COMPONENT_TYPE_COUNT // must be last
};

class Component
{
    friend class GameManager;

public:
    Component(GameObject *gameObject, ComponentType type);
    virtual ~Component() = default;

    GameObject *getGameObject() const;
    ComponentType getType() const;

private:
    GameObject *gameObject;
    ComponentType type;
};

class GameObject
{
    friend class GameManager;

public:
    GameObject(GameObject &) = delete;
    GameObject &operator=(const GameObject &) = delete;

    // Getters
    float getRotation() const;
    float getRotationInDegrees() const;
    float getCosRotation() const;
    float getSinRotation() const;

    bool getActive() const;
    const Eigen::Vector2f &getPosition() const;
    float getX() const;
    float getY() const;
    const Eigen::Vector2f &getScale() const;
    const std::string &getName() const;
    const std::string &getTag() const;

    Component *getComponent(const ComponentType &componentType) const;
    Component **getComponents();

    template <typename T>
    T *getComponent() const;
    template <typename T, typename... Args>
    GameObject *addComponent(Args &&...args);

    const std::list<GameObject *> &getChildren() const;

    // Setters
    GameObject *setRotation(const float rotation);
    GameObject *setActive(const bool active);
    GameObject *setPosition(const Eigen::Vector2f &position);
    GameObject *setScale(const Eigen::Vector2f &scale);
    GameObject *setName(const std::string &name);
    GameObject *setTag(const std::string &tag);
    GameObject *setIsMarkedForDeletion(const bool markedForDeletion);

    void addComponent(Component *component);
    bool removeComponent(const ComponentType &componentType);
    void addChild(GameObject *child);
    bool removeChild(GameObject *child);
    bool getIsMarkedForDeletion() const;

private:
    GameObject();
    GameObject(const float rotation, const bool active, const Eigen::Vector2f &position, const Eigen::Vector2f &scale, const std::string &name, const std::string &tag);

    ~GameObject();

    // rotation is in radians
    float rotation;
    float sinRotation;
    float cosRotation;

    bool isActive;
    Eigen::Vector2f position;
    Eigen::Vector2f scale;
    std::string name;
    std::string tag;

    Component *components[COMPONENT_TYPE_COUNT];
    std::list<GameObject *> children;
    bool markedForDeletion;

    void updateCollider();
    void addComponentInternal(Component *component);
};

template <typename T>
struct ComponentTypeFor;

template <typename T>
T *GameObject::getComponent() const
{
    Component *component = components[ComponentTypeFor<T>::value];
    return component ? static_cast<T *>(component) : nullptr;
}

template <typename T, typename... Args>
GameObject *GameObject::addComponent(Args &&...args)
{
    static_assert(std::is_base_of_v<Component, T>, "addComponent<T> requires T to derive from Component");
    static_assert(requires { ComponentTypeFor<T>::value; }, "addComponent<T> requires a visible ComponentTypeFor<T>::value specialization");
    static_assert(std::is_constructible_v<T, GameObject *, Args...>, "addComponent<T>(args...) requires a matching constructor T(GameObject*, args...)");

    // this is so if addComponent throws an exception, we won't have memory leak
    std::unique_ptr<T> component = std::make_unique<T>(this, std::forward<Args>(args)...);
    addComponent(component.get());
    component.release();

    return this;
}