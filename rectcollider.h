#pragma once

#include "collider.h"

class RectCollider : public Collider
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