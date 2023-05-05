#pragma once

#include "collider.h"

class RectCollider : public Collider
{
public:
    RectCollider(GameObject* gameObject);
    RectCollider(GameObject* gameObject, const float width, const float height);
    ~RectCollider();

    // Inherited via Collider
    float getBoundingRadius() const override;

    // Getters
    float getWidth() const;
    float getHeight() const;

    // Setters
    void setWidth(const float width);
    void setHeight(const float height);
    
private:
    float width;
    float height;
    float boundingRadius;
};