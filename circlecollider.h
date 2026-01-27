#pragma once

#include "collider.h"

class CircleCollider : public Collider
{
public:
    CircleCollider(GameObject *gameObject);
    ~CircleCollider();

    // Inherited via Collider
    float getBoundingBoxLengthX() const override;
    float getBoundingBoxLengthY() const override;
    ColliderType getColliderType() const override;
    std::vector<Eigen::Vector2f *> *getNormals(Collider *other) override;
    std::unique_ptr<Eigen::Vector2f> projectOntoAxis(const Eigen::Vector2f &axis) override;
    std::unique_ptr<Eigen::Vector2f> projectOntoAxis(const Eigen::Vector2f &axis, size_t index) override;

    // Getters
    float getRadius() const;

    // Setters
    void setRadius(const float radius);

private:
    float radius;
};
