#pragma once

#include "collider.h"

class CircleCollider : public Collider
{
public:
    CircleCollider(GameObject* gameObject);
    ~CircleCollider();

    // Inherited via Collider
    float getBoundingRadius() const override;

    // Getters
    float getRadius() const;

    // Setters
    void setRadius(const float radius);

private:
    float radius;
};
