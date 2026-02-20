#pragma once

#include <eigen3/Eigen/Dense>
#include <list>

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
public:
    Component(GameObject *gameObject, ComponentType type);
    ~Component();

    GameObject *getGameObject();
    ComponentType getType();

private:
    GameObject *gameObject;
    ComponentType type;
};

class GameObject
{
public:
    GameObject();
    GameObject(const float rotation, const bool active, const Eigen::Vector2f &position, const Eigen::Vector2f &scale, const std::string &name, const std::string &tag);

    ~GameObject();

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
    const std::list<GameObject *> &getChildren() const;

    // Setters
    void setRotation(const float rotation);
    void setActive(const bool active);
    void setPosition(const Eigen::Vector2f &position);
    void setScale(const Eigen::Vector2f &scale);
    void setName(const std::string &name);
    void setTag(const std::string &tag);

    bool addComponent(Component *component);
    bool removeComponent(const ComponentType &componentType);
    bool addChild(GameObject *child);
    bool removeChild(GameObject *child);

private:
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

    void updateCollider();
};