#pragma once

#include "collider.h"

#include <json.hpp>
#include "jsonhelpers.h"

class CircleCollider : public Collider
{
public:
    CircleCollider(GameObject *gameObject, const float radius);
    ~CircleCollider();

    // Inherited via Collider
    ColliderType getColliderType() const override;
    std::vector<Eigen::Vector2f> getNormals(const Collider *other) const override;
    Eigen::Vector2f projectOntoAxis(const Eigen::Vector2f &axis) const override;

    // Getters
    float getRadius() const;

    // Setters
    void setRadius(const float radius);

    Edge getEdgeWithNormal(const Eigen::Vector2f &normal) const override;

private:
    float radius;

    void generateProjections();
    void updateCollider() override;
};

template <>
struct ComponentTypeFor<CircleCollider>
{
    static constexpr ComponentType value = ComponentType::COLLIDER;
};

void to_json(nlohmann::json &j, const CircleCollider &circleCollider);
void from_json(const nlohmann::json &j, CircleCollider &circleCollider);