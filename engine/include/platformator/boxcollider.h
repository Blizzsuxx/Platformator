#pragma once

#include <array>
#include <cmath>
#include <new>

#include <json.hpp>

#include "platformator/collider.h"

class BoxCollider : public Collider
{
  public:
    PLATFORMATOR_DECLARE_POOLED_ALLOCATORS()

    BoxCollider(GameObject *gameObject, float width, float height);
    BoxCollider();
    ~BoxCollider();
    Component *clone() const override;

    float getWidth() const;
    float getHeight() const;
    float getWidthWithoutScale() const;
    float getHeightWithoutScale() const;

    void setWidth(float width);
    void setHeight(float height);

    const std::array<Eigen::Vector2f, 4> &getVertices() const;
    const std::array<Eigen::Vector2f, 2> &getAxes() const
    {
        return normals;
    }
    float getPublishedHalfWidth() const noexcept
    {
        return publishedHalfWidth;
    }
    float getPublishedHalfHeight() const noexcept
    {
        return publishedHalfHeight;
    }
    const Eigen::Vector2f &getPublishedXAxis() const noexcept
    {
        return normals[0];
    }
    Edge getSupportEdge(const Eigen::Vector2f &normal, Eigen::Vector2f *outUnitTangent = nullptr) const noexcept
    {
        const float projX = normal.dot(normals[0]);
        const float projY = normal.dot(normals[1]);

        if (std::abs(projX) > std::abs(projY))
        {
            if (projX > 0.0f)
            {
                if (outUnitTangent != nullptr)
                    *outUnitTangent = normals[1];
                return Edge(vertices[1], vertices[2], EDGE4);
            }
            if (outUnitTangent != nullptr)
                *outUnitTangent = -normals[1];
            return Edge(vertices[3], vertices[0], EDGE2);
        }

        if (projY > 0.0f)
        {
            if (outUnitTangent != nullptr)
                *outUnitTangent = -normals[0];
            return Edge(vertices[2], vertices[3], EDGE3);
        }
        if (outUnitTangent != nullptr)
            *outUnitTangent = normals[0];
        return Edge(vertices[0], vertices[1], EDGE1);
    }

  private:
    float width;
    float height;
    float publishedHalfWidth;
    float publishedHalfHeight;
    std::array<Eigen::Vector2f, 4> vertices;
    std::array<Eigen::Vector2f, 2> normals;

    void updateCollider() final;
};

template <> struct ComponentTypeFor<BoxCollider>
{
    static constexpr ComponentType value = ComponentType::COLLIDER;
};

void to_json(nlohmann::json &j, const BoxCollider &boxCollider);
void from_json(const nlohmann::json &j, BoxCollider &boxCollider);
