#pragma once

#include <cstdio>
#include <Eigen/Dense>
#include <array>
#include <cstddef>

inline float cross2D(const Eigen::Vector2f &a, const Eigen::Vector2f &b)
{
    return a.x() * b.y() - a.y() * b.x();
}

inline Eigen::Vector2f crossSV(float s, const Eigen::Vector2f &v)
{
    return Eigen::Vector2f(-s * v.y(), s * v.x());
}

struct Edge
{
    Eigen::Vector2f max;
    Eigen::Vector2f v1;
    Eigen::Vector2f v2;

    Edge() : max(Eigen::Vector2f::Zero()), v1(Eigen::Vector2f::Zero()), v2(Eigen::Vector2f::Zero())
    {
    }

    Edge(const Eigen::Vector2f &max, const Eigen::Vector2f &v1, const Eigen::Vector2f &v2)
        : max(max), v1(v1), v2(v2)
    {
    }

    Eigen::Vector2f getEdgeVector() const
    {
        return v2 - v1;
    }
};

struct ClipPoints
{
    std::array<Eigen::Vector2f, 3> points;
    size_t count;

    ClipPoints() : points(), count(0)
    {
    }

    void add(const Eigen::Vector2f &point)
    {
        if (count < points.size())
        {
            points[count++] = point;
        }
        else
        {
            printf("Warning: ClipPoints overflow, point not added\n");
        }
    }

    void remove(size_t index)
    {
        if (index < count)
        {
            for (size_t i = index; i + 1 < count; ++i)
            {
                points[i] = points[i + 1];
            }
            --count;
        }
    }
};

struct ClipPointWithData
{
    Eigen::Vector2f point;
    float separation;
    float accumulatedNormalImpulse;
    float accumulatedTangentImpulse;
    float accumulatedNormalImpulseBias;
    float massNormal;
    float massTangent;
    float bias;

    ClipPointWithData() : point(Eigen::Vector2f::Zero()), separation(0.0f), accumulatedNormalImpulse(0.0f), accumulatedTangentImpulse(0.0f), accumulatedNormalImpulseBias(0.0f), massNormal(0.0f), massTangent(0.0f), bias(0.0f)
    {
    }

    ClipPointWithData(const Eigen::Vector2f &point) : point(point), separation(0.0f), accumulatedNormalImpulse(0.0f), accumulatedTangentImpulse(0.0f), accumulatedNormalImpulseBias(0.0f), massNormal(0.0f), massTangent(0.0f), bias(0.0f)
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

    size_t findIndex(const Eigen::Vector2f &point) const
    {
        for (size_t i = 0; i < count; ++i)
        {
            if (points[i].point == point)
            {
                return i;
            }
        }
        return SIZE_MAX;
    }
};

// clips the line segment points v1, v2
// if they are past o along n
inline ClipPoints clip(const Eigen::Vector2f &v1, const Eigen::Vector2f &v2, const Eigen::Vector2f &n, float o)
{
    ClipPoints cp;
    double d1 = n.dot(v1) - o;
    double d2 = n.dot(v2) - o;
    // if either point is past o along n
    // then we can keep the point
    if (d1 >= 0.0)
        cp.add(v1);
    if (d2 >= 0.0)
        cp.add(v2);
    // finally we need to check if they
    // are on opposing sides so that we can
    // compute the correct point
    if (d1 * d2 < 0.0)
    {
        // if they are on different sides of the
        // offset, d1 and d2 will be a (+) * (-)
        // and will yield a (-) and therefore be
        // less than zero
        // get the vector for the edge we are clipping
        Eigen::Vector2f e = v2 - v1;
        // compute the location along e
        double u = d1 / (d1 - d2);
        e *= u;
        e += v1;
        // add the point
        cp.add(e);
    }
    return cp;
}

inline float Min(float a, float b)
{
    return a < b ? a : b;
}

inline float Max(float a, float b)
{
    return a > b ? a : b;
}

inline float Clamp(float a, float low, float high)
{
    return Max(low, Min(a, high));
}