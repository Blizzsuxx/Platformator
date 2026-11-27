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
};

struct ClipPointsWithData
{
    struct WarmStartSnapshot
    {
        uint32_t count;
        uint32_t key0;
        uint32_t key1;
        float normal0;
        float tangent0;
        float normal1;
        float tangent1;

        void restore(ClipPointWithData &dst, uint32_t key) const noexcept
        {
            if (count > 0 && key == key0)
            {
                dst.accumulatedNormalImpulse = normal0;
                dst.accumulatedTangentImpulse = tangent0;
            }
            else if (count > 1 && key == key1)
            {
                dst.accumulatedNormalImpulse = normal1;
                dst.accumulatedTangentImpulse = tangent1;
            }
            else
            {
                dst.accumulatedNormalImpulse = 0.0f;
                dst.accumulatedTangentImpulse = 0.0f;
            }
        }
    };

    std::array<ClipPointWithData, 2> points;
    uint32_t count;

    ClipPointsWithData() : points(), count(0)
    {
    }

    WarmStartSnapshot snapshotWarmStart() const noexcept
    {
        return WarmStartSnapshot{
            count,
            count > 0 ? points[0].point.feature.key : 0u,
            count > 1 ? points[1].point.feature.key : 0u,
            count > 0 ? points[0].accumulatedNormalImpulse : 0.0f,
            count > 0 ? points[0].accumulatedTangentImpulse : 0.0f,
            count > 1 ? points[1].accumulatedNormalImpulse : 0.0f,
            count > 1 ? points[1].accumulatedTangentImpulse : 0.0f,
        };
    }

    void setSinglePoint(const Eigen::Vector2f &point, ContactFeature feature, float separation) noexcept
    {
        const WarmStartSnapshot warm = snapshotWarmStart();
        ClipPointWithData &dst = points[0];
        dst.point.point = point;
        dst.point.feature = feature;
        dst.separation = separation;
        warm.restore(dst, feature.key);
        count = 1;
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
