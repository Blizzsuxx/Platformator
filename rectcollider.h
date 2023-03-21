#pragma once

#include <eigen3/Eigen/Dense>
#include "component.h"

class RectCollider : public Component
{
public:
    RectCollider(GameObject* gameObject);
    ~RectCollider();

    // Getters
    float getWidth() const;
    float getHeight() const;

    // Setters
    void setWidth(const float width);
    void setHeight(const float height);
    
private:
    float width;
    float height;
};