#pragma once

#include <eigen3/Eigen/Dense>
#include "component.h"

class CircleCollider : public Component
{
public:
    CircleCollider(GameObject* gameObject);
    ~CircleCollider();

    // Getters
    float getRadius() const;

    // Setters
    void setRadius(const float radius);

private:
    float radius;
};
