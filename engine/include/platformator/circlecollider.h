#pragma once

#include <json.hpp>

#include "platformator/collider.h"

class CircleCollider : public Collider
{
public:
    static void *operator new(size_t size);
    static void operator delete(void *ptr, size_t size) noexcept;
    static void *operator new(size_t, void *ptr) noexcept
    {
        return ptr;
    }
    static void operator delete(void *, void *) noexcept
    {
    }

    CircleCollider();
    CircleCollider(GameObject *gameObject, float radius);
    ~CircleCollider();
    Component *clone() const override;

    std::vector<Eigen::Vector2f> getNormals(const Collider *other) const override;
    Eigen::Vector2f projectOntoAxis(const Eigen::Vector2f &axis) const override;

    float getRadius() const;
    float getRadiusWithoutScale() const;
    float getPublishedRadius() const noexcept
    {
        return publishedRadius;
    }

    void setRadius(float radius);

    Edge getEdgeWithNormal(const Eigen::Vector2f &normal) const override;

private:
    float radius;
    float publishedRadius;

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