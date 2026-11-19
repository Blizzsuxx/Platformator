#pragma once

#include <array>
#include <new>

#include <json.hpp>

#include "platformator/collider.h"

class BoxCollider : public Collider
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
    static void *operator new(size_t size, std::align_val_t alignment)
    {
        return ::operator new(size, alignment);
    }
    static void operator delete(void *ptr, size_t size, std::align_val_t alignment) noexcept
    {
        ::operator delete(ptr, size, alignment);
    }
    static void operator delete(void *ptr, std::align_val_t alignment) noexcept
    {
        ::operator delete(ptr, alignment);
    }

    BoxCollider(GameObject *gameObject, float width, float height);
    BoxCollider();
    ~BoxCollider();
    Component *clone() const override;

    Eigen::Vector2f projectOntoAxis(const Eigen::Vector2f &axis) const override;

    float getWidth() const;
    float getHeight() const;
    float getWidthWithoutScale() const;
    float getHeightWithoutScale() const;

    void setWidth(float width);
    void setHeight(float height);

    std::vector<Eigen::Vector2f> getNormals(const Collider *other) const override;

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
    Edge getEdgeWithNormal(const Eigen::Vector2f &normal) const override;

  private:
    float width;
    float height;
    float publishedHalfWidth;
    float publishedHalfHeight;
    std::array<Eigen::Vector2f, 4> vertices;
    std::array<Eigen::Vector2f, 2> normals;

    void generateNormals();
    void generateProjections();
    void generateVertices();
    void updateCollider() override;
};

template <> struct ComponentTypeFor<BoxCollider>
{
    static constexpr ComponentType value = ComponentType::COLLIDER;
};

void to_json(nlohmann::json &j, const BoxCollider &boxCollider);
void from_json(const nlohmann::json &j, BoxCollider &boxCollider);
