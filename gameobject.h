#pragma once

#include <eigen3/Eigen/Dense>
#include <list>

class GameObject;

constexpr auto _START = __LINE__;
enum ComponentType
{
    ANIMATOR = 0,
    AUDIO = 1,
    CAMERA = 2,
    COLLIDER = 3,
    LIGHT = 4,
    RIDIDBODY = 5,
    SPRITE = 6
};
constexpr auto COMPONENT_TYPE_COUNT = __LINE__ - _START - 4;

class Component
{
public:
    Component(GameObject* gameObject, ComponentType type);
    virtual ~Component();

    GameObject* getGameObject();
    ComponentType getType();
private:
    GameObject* gameObject;
    ComponentType type;
};

class GameObject
{
public:
    GameObject();
    GameObject(const std::string& name, const std::string& tag);
    GameObject(const std::string& name, const std::string& tag, const Eigen::Vector2f& position);

    ~GameObject();

    // Getters
    Eigen::Vector2f getPosition() const;
    std::string getName() const;
    std::string getTag() const;

    Component* getComponent(const ComponentType& componentType) const;
    std::list<Component*> getComponents() const;
    std::list<GameObject*> getChildren() const;

    // Setters
    void setPosition(const Eigen::Vector2f& position);
    void setName(const std::string& name);
    void setTag(const std::string& tag);

    bool addComponent(Component* component);
    bool removeComponent(const ComponentType& componentType);
    bool addChild(GameObject* child);
    bool removeChild(GameObject* child);
private:
    Eigen::Vector2f position;
    std::string name;
    std::string tag;
    
    Component* components[COMPONENT_TYPE_COUNT];
    std::list<GameObject*> children;
};