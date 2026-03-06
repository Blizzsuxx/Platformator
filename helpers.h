#pragma once

#include <eigen3/Eigen/Dense>

inline float cross2D(const Eigen::Vector2f &a, const Eigen::Vector2f &b)
{
    return a.x() * b.y() - a.y() * b.x();
}

inline float cross2D(const Eigen::Vector2f &v, float s)
{
    return v.x() * s - v.y() * s;
}

inline Eigen::Vector2f crossSV(float s, const Eigen::Vector2f &v)
{
    return Eigen::Vector2f(-s * v.y(), s * v.x());
}