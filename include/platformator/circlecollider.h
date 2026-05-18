#pragma once

#include <json.hpp>

#include "platformator/collider.h"

class CircleCollider : public Collider
{
public:
    CircleCollider();
    CircleCollider(GameObject *gameObject, float radius);
    ~CircleCollider();

    std::vector<Eigen::Vector2f> getNormals(const Collider *other) const override;
    Eigen::Vector2f projectOntoAxis(const Eigen::Vector2f &axis) const override;

    float getRadius() const;
    float getRadiusWithoutScale() const;

    void setRadius(float radius);

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