#pragma once

#include "collider.h"

class RectCollider : public Collider
{
public:
    RectCollider(GameObject *gameObject);
    RectCollider(GameObject *gameObject, const float width, const float height);
    ~RectCollider();

    // Inherited via Collider
    float getBoundingBoxLengthX() const override;
    float getBoundingBoxLengthY() const override;
    ColliderType getColliderType() const override;
    std::auto_ptr<std::vector<Eigen::Vector2f>> getNormals(Collider *other) override;
    std::auto_ptr<std::vector<Eigen::Vector2f>> getVertices() override;
    std::auto_ptr<Eigen::Vector2f> projectOntoAxis(const Eigen::Vector2f &axis) override;
    std::auto_ptr<Eigen::Vector2f> projectOntoAxis(const Eigen::Vector2f &axis, size_t index) override;

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