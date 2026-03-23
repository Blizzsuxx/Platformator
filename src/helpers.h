#pragma once

#include <Eigen/Dense>

inline float cross2D(const Eigen::Vector2f &a, const Eigen::Vector2f &b)
{
    return a.x() * b.y() - a.y() * b.x();
}

inline Eigen::Vector2f crossSV(float s, const Eigen::Vector2f &v)
{
    return Eigen::Vector2f(-s * v.y(), s * v.x());
}