#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <Eigen/Dense>
#include <json.hpp>

#include "platformator/baseobject.h"

class GameManager;
class GameObject;
class Scene;
class SceneBuildContext;

enum ComponentType
{
    ANIMATOR = 0,
    AUDIO,
    CAMERA,
    COLLIDER,
    RIGID_BODY,
    SCRIPT,
    SPRITE,
    COMPONENT_TYPE_COUNT
};

enum GameObjectFlags : uint8_t
{
    GAME_OBJECT_NONE = 0,
    IS_MARKED_FOR_DELETION = 1 << 0,
    IS_REGISTERED_IN_GAME_MANAGER = 1 << 1,
    IS_ACTIVE = 1 << 2
};

class Component : public BaseObject
{
    friend class GameManager;

  public:
    Component(GameObject *gameObject, ComponentType type);
    Component(ComponentType type);
    virtual ~Component() = default;

    GameObject *getGameObject() const;
    ComponentType getType() const;
    bool getIsCallbackEligible() const
    {
        return callbackEligible;
    }

    virtual void setGameObject(GameObject *gameObject);

  private:
    bool reconcileQueued = false;
    bool systemsRegistered = false;
    bool callbackEligible = false;
    GameObject *gameObject;
    ComponentType type;
};

class GameObject : public BaseObject
{
    friend class GameManager;
    friend class Scene;
    friend class SceneBuildContext;
    friend class Rigidbody;
    friend class PhysicsManager;
    friend class Collider;
    friend class Collision;
    friend class Camera;
    friend void from_json(const nlohmann::json &j, GameObject &gameObject);

  public:
    GameObject(GameObject &) = delete;
    GameObject &operator=(const GameObject &) = delete;

    float getRotation() const;
    float getRotationInDegrees() const;
    float getCosRotation() const;
    float getSinRotation() const;

    bool getActive() const;
    Eigen::Vector2f getPosition() const;
    float getX() const;
    float getY() const;
    const Eigen::Vector2f &getScale() const;
    const std::string &getName() const;
    const std::string &getTag() const;
    GameObject *getParent() const;

    Component *getComponent(const ComponentType &componentType) const;
    Component *const *getComponents() const;

    template <typename T> T *getComponent() const;

    template <typename T, typename... Args> GameObject *addComponent(Args &&...args);

    const std::vector<GameObject *> &getChildren() const;

    GameObject *setRotation(float rotation);
    GameObject *setActive(bool active);
    GameObject *setPosition(const Eigen::Vector2f &position);
    GameObject *setScale(const Eigen::Vector2f &scale);
    GameObject *setName(const std::string &name);
    GameObject *setTag(const std::string &tag);

    void addComponent(Component *component);
    bool removeComponent(const ComponentType &componentType);
    void addChild(GameObject *child);
    bool removeChild(GameObject *child);
    bool getIsMarkedForDeletion() const;
    void destroy();

  private:
    GameObject();
    ~GameObject();

    float rotation;
    float sinRotation;
    float cosRotation;

    GameObject *parent;
    size_t parentIndex = SIZE_MAX;
    Eigen::Vector2f position;
    // Immediate public position; worldPosition remains the last synchronized snapshot.
    Eigen::Vector2f immediateWorldPosition = Eigen::Vector2f::Zero();
    Eigen::Vector2f worldPosition = Eigen::Vector2f::Zero();
    size_t transformQueueIndex = SIZE_MAX;
    size_t hierarchyOrder = 0;
    uint64_t transformStamp = 0;
    Eigen::Vector2f scale;
    std::string name;
    std::string tag;

    Component *components[COMPONENT_TYPE_COUNT];
    std::vector<GameObject *> children;
    size_t gameManagerIteratorIndex;
    uint8_t flags;

    void validateMutation() const;
    void detachFromParent();
    void updateCollider();
    void queueTransform();
    void refreshImmediatePositions();
    void addComponentInternal(Component *component);
    void attachComponent(Component *component);
    void flushMutations();
    Component *getColliderByShape(bool box) const;
    GameObject *setIsMarkedForDeletion(bool markedForDeletion);
    GameObject *setIsRegisteredInGameManager(bool isRegisteredInGameManager);
    GameObject *setGameManagerIteratorIndex(size_t index);
    bool getIsRegisteredInGameManager() const;
    void addComponentsToGameManager();
};

template <typename T> struct ComponentTypeFor;

template <typename T> T *GameObject::getComponent() const
{
    if constexpr (std::is_same_v<T, BoxCollider>)
        return static_cast<T *>(getColliderByShape(true));
    else if constexpr (std::is_same_v<T, CircleCollider>)
        return static_cast<T *>(getColliderByShape(false));
    Component *component = components[ComponentTypeFor<T>::value];
    return component ? static_cast<T *>(component) : nullptr;
}

template <typename T, typename... Args> GameObject *GameObject::addComponent(Args &&...args)
{
    static_assert(std::is_base_of_v<Component, T>, "addComponent<T> requires T to derive from Component");
    static_assert(
        requires { ComponentTypeFor<T>::value; },
        "addComponent<T> requires a visible ComponentTypeFor<T>::value specialization");
    static_assert(std::is_constructible_v<T, GameObject *, Args...>,
                  "addComponent<T>(args...) requires a matching constructor T(GameObject*, args...)");

    validateMutation();
    std::unique_ptr<T> component = std::make_unique<T>(this, std::forward<Args>(args)...);
    attachComponent(component.get());
    component.release();
    flushMutations();

    return this;
}

void to_json(nlohmann::json &j, const GameObject &gameObject);
void from_json(const nlohmann::json &j, GameObject &gameObject);
