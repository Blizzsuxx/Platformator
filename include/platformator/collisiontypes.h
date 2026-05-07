#pragma once

#include <array>
#include <cstddef>

#include <Eigen/Dense>

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

    explicit ClipPointWithData(const Eigen::Vector2f &point) : point(point), separation(0.0f), accumulatedNormalImpulse(0.0f), accumulatedTangentImpulse(0.0f), accumulatedNormalImpulseBias(0.0f), massNormal(0.0f), massTangent(0.0f), bias(0.0f)
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

        return static_cast<size_t>(-1);
    }
};