#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <Eigen/Dense>

class Collider;
class GameObject;

enum EdgeNumber : uint8_t
{
    NO_EDGE = 0,
    EDGE1 = 1,
    EDGE2 = 2,
    EDGE3 = 3,
    EDGE4 = 4,
};

union ContactFeature
{
    struct Edges
    {
        uint8_t inEdge1;
        uint8_t outEdge1;
        uint8_t inEdge2;
        uint8_t outEdge2;
    } e;
    uint32_t key;

    constexpr ContactFeature() : key(0)
    {
    }
    constexpr ContactFeature(uint32_t key) : key(key)
    {
    }
    constexpr ContactFeature(uint8_t inEdge1, uint8_t outEdge1, uint8_t inEdge2, uint8_t outEdge2)
        : e{inEdge1, outEdge1, inEdge2, outEdge2}
    {
    }
};

constexpr ContactFeature makeContactFeature(EdgeNumber inEdge1, EdgeNumber outEdge1, EdgeNumber inEdge2, EdgeNumber outEdge2)
{
    return ContactFeature(static_cast<uint8_t>(inEdge1), static_cast<uint8_t>(outEdge1), static_cast<uint8_t>(inEdge2), static_cast<uint8_t>(outEdge2));
}

struct Edge
{
    Eigen::Vector2f v1;
    Eigen::Vector2f v2;
    EdgeNumber edgeNumber;

    Edge(const Eigen::Vector2f &v1, const Eigen::Vector2f &v2, EdgeNumber edgeNumber)
        : v1(v1), v2(v2), edgeNumber(edgeNumber)
    {
    }
};

struct ClipVertex
{
    Eigen::Vector2f point;
    ContactFeature feature;

    ClipVertex() : point(Eigen::Vector2f::Zero()), feature()
    {
    }

    ClipVertex(const Eigen::Vector2f &point, const ContactFeature &feature)
        : point(point), feature(feature)
    {
    }
};

struct ClipPoints
{
    std::array<ClipVertex, 2> points;
    size_t count;

    ClipPoints() : points(), count(0)
    {
    }

    void add(const ClipVertex &point)
    {
        if (count < points.size())
        {
            points[count++] = point;
        }
    }

    void add(const Eigen::Vector2f &point, const ContactFeature &feature)
    {
        add(ClipVertex(point, feature));
    }
};

struct ClipPointWithData
{
    ClipVertex point;
    float separation;
    float accumulatedNormalImpulse;
    float accumulatedTangentImpulse;

    ClipPointWithData()
        : point(Eigen::Vector2f::Zero(), ContactFeature()), separation(0.0f), accumulatedNormalImpulse(0.0f),
          accumulatedTangentImpulse(0.0f)
    {
    }

    explicit ClipPointWithData(const ClipVertex &point)
        : point(point), separation(0.0f), accumulatedNormalImpulse(0.0f), accumulatedTangentImpulse(0.0f)
    {
    }
};

struct ClipPointsWithData
{
    std::array<ClipPointWithData, 2> points;
    size_t count;

    ClipPointsWithData() : points(), count(0)
    {
    }

    size_t findIndex(const ContactFeature &feature) const
    {
        for (size_t i = 0; i < count; ++i)
        {
            if (points[i].point.feature.key == feature.key)
            {
                return i;
            }
        }

        return SIZE_MAX;
    }
};

namespace platformator
{
struct RaycastHit
{
    Collider *collider = nullptr;
    GameObject *gameObject = nullptr;
    Eigen::Vector2f point = Eigen::Vector2f::Zero();
    Eigen::Vector2f normal = Eigen::Vector2f::Zero();
    float distance = 0.0f;
    float fraction = 0.0f;
};
} // namespace platformator
