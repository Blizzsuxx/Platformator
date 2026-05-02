#pragma once

#include "collider.h"
#include "constants.h"

#include <json.hpp>
#include "jsonhelpers.h"

class BoxCollider : public Collider
{
public:
    BoxCollider(GameObject *gameObject, const float width, const float height);
    BoxCollider();
    ~BoxCollider();

    // Inherited via Collider
    ColliderType getColliderType() const override;
    Eigen::Vector2f projectOntoAxis(const Eigen::Vector2f &axis) const override;

    // Getters
    float getWidth() const;
    float getHeight() const;
    float getWidthWithoutScale() const;
    float getHeightWithoutScale() const;

    // Setters
    void setWidth(const float width);
    void setHeight(const float height);

    std::vector<Eigen::Vector2f> getNormals(const Collider *other) const override;

    const std::array<Eigen::Vector2f, 4> &getVertices() const;
    Edge getEdgeWithNormal(const Eigen::Vector2f &normal) const override;

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

void to_json(nlohmann::json &j, const BoxCollider &boxCollider);
void from_json(const nlohmann::json &j, BoxCollider &boxCollider);