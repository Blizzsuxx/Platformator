#pragma once

#include "collider.h"

class BoxCollider : public Collider
{
public:
    BoxCollider(GameObject *gameObject);
    BoxCollider(GameObject *gameObject, const float width, const float height);
    ~BoxCollider();

    // Inherited via Collider
    ColliderType getColliderType() const override;
    Eigen::Vector2f projectOntoAxis(const Eigen::Vector2f &axis) override;

    // Getters
    float getWidth() const;
    float getHeight() const;

    // Setters
    void setWidth(const float width);
    void setHeight(const float height);

private:
    float width;
    float height;
    std::array<Eigen::Vector2f, 4> vertices;

    std::array<Eigen::Vector2f, 4> &getVertices();

    void generateNormals();
    void generateProjections();
    void generateVertices();

    void updateCollider() override;
};