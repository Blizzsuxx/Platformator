#pragma once

#include "collider.h"

class CircleCollider : public Collider
{
public:
    CircleCollider(GameObject *gameObject);
    ~CircleCollider();

    // Inherited via Collider
    ColliderType getColliderType() const override;
    std::vector<Eigen::Vector2f> getNormals(const Collider *other) const override;
    Eigen::Vector2f projectOntoAxis(const Eigen::Vector2f &axis) const override;

    // Getters
    float getRadius() const;

    // Setters
    void setRadius(const float radius);

private:
    float radius;

    void generateProjections();

    void updateCollider() override;
};
