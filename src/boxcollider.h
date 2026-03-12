#pragma once

#include "collider.h"
#include "constants.h"

class BoxCollider : public Collider
{
public:
    BoxCollider(GameObject *gameObject);
    BoxCollider(GameObject *gameObject, const float width, const float height);
    ~BoxCollider();

    // Inherited via Collider
    ColliderType getColliderType() const override;
    Eigen::Vector2f projectOntoAxis(const Eigen::Vector2f &axis) const override;

    // Getters
    float getWidth() const;
    float getHeight() const;

    // Setters
    void setWidth(const float width);
    void setHeight(const float height);

    std::vector<Eigen::Vector2f> getNormals(const Collider *other) const override;

    const std::array<Eigen::Vector2f, 4> &getVertices() const;

private:
    float width;
    float height;
    std::array<Eigen::Vector2f, 4> vertices;
    std::vector<Eigen::Vector2f> normals;
    void generateNormals();
    void generateProjections();
    void generateVertices();

    void updateCollider() override;
};

template <>
struct ComponentTypeFor<BoxCollider>
{
    static constexpr ComponentType value = ComponentType::COLLIDER;
};