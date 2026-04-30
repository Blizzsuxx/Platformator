#pragma once

#include "collider.h"
#include "constants.h"

#include <json.hpp>
#include "jsonhelpers.h"

class BoxCollider : public Collider
{
public:
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

void to_json(nlohmann::json &j, const BoxCollider &boxCollider)
{
    j = nlohmann::json{{"width", boxCollider.getWidth()}, {"height", boxCollider.getHeight(), "collisionGroup", boxCollider.getCollisionGroup()}, {"collisionMask", boxCollider.getCollisionMask()}};
}
void from_json(const nlohmann::json &j, BoxCollider &boxCollider)
{
    boxCollider.setWidth(j.at("width").get<float>());
    boxCollider.setHeight(j.at("height").get<float>());
    boxCollider.setCollisionGroup(j.at("collisionGroup").get<uint64_t>());
    boxCollider.setCollisionMask(j.at("collisionMask").get<uint64_t>());
}