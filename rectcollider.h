#pragma once

#include <eigen3/Eigen/Dense>
#include "component.h"

class RectCollider : public Component
{
public:
    RectCollider(GameObject* gameObject);
    ~RectCollider();

    // Getters
    Eigen::Vector2f getMin() const;
    Eigen::Vector2f getMax() const;

    // Setters
    void setMin(const Eigen::Vector2f& min);
    void setMax(const Eigen::Vector2f& max);

private:
    Eigen::Vector2f min;
    Eigen::Vector2f max;
};