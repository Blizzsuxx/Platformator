#pragma once

#include <new>

#include <json.hpp>

#include "platformator/collider.h"

class CircleCollider : public Collider
{
  public:
    PLATFORMATOR_DECLARE_POOLED_ALLOCATORS()

    CircleCollider();
    CircleCollider(GameObject *gameObject, float radius);
    ~CircleCollider();
    Component *clone() const override;

    float getRadius() const;
    float getRadiusWithoutScale() const;
    float getPublishedRadius() const noexcept
    {
        return publishedRadius;
    }

    void setRadius(float radius);

  private:
    float radius;
    float publishedRadius;

    void updateCollider() final;
};

template <> struct ComponentTypeFor<CircleCollider>
{
    static constexpr ComponentType value = ComponentType::COLLIDER;
};

void to_json(nlohmann::json &j, const CircleCollider &circleCollider);
void from_json(const nlohmann::json &j, CircleCollider &circleCollider);
