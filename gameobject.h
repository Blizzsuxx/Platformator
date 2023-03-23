#pragma once

#include <eigen3/Eigen/Dense>
#include <list>
#include "component.h"

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