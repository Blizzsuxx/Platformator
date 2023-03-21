#pragma once

#include <eigen3/Eigen/Dense>
#include <list>
#include "component.h"

class GameObject
{
public:
    GameObject();
    ~GameObject();

    Eigen::Vector2f getPosition() const;
    void setPosition(const Eigen::Vector2f& position);

private:
    Eigen::Vector2f position;
    std::list<Component*> components;
};