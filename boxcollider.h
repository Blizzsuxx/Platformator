#pragma once

#include "collider.h"

class BoxCollider : public Collider
{
public:
    BoxCollider(GameObject *gameObject);
    BoxCollider(GameObject *gameObject, const float width, const float height);
    ~BoxCollider();

    // Inherited via Collider
    float getBoundingBoxLengthX() const override;
    float getBoundingBoxLengthY() const override;
    ColliderType getColliderType() const override;
    std::vector<Eigen::Vector2f*> *getNormals(Collider *other) override;
    std::unique_ptr<Eigen::Vector2f> projectOntoAxis(const Eigen::Vector2f &axis) override;
    std::unique_ptr<Eigen::Vector2f> projectOntoAxis(const Eigen::Vector2f &axis, size_t index) override;

    // Getters
    float getWidth() const;
    float getHeight() const;

    // Setters
    void setWidth(const float width);
    void setHeight(const float height);
    
private:
    float width;
    float height;

    std::unique_ptr<std::vector<Eigen::Vector2f>> getVertices();
};