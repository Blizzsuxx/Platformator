#pragma once

#include <Eigen/Dense>

#include "platformator/collisiontypes.h"

#include "scriptcomponent.h"
#include "gameobject.h"

inline float cross2D(const Eigen::Vector2f &a, const Eigen::Vector2f &b)
{
    return a.x() * b.y() - a.y() * b.x();
}

inline Eigen::Vector2f crossSV(float s, const Eigen::Vector2f &v)
{
    return Eigen::Vector2f(-s * v.y(), s * v.x());
}

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

template <typename T>
T *getBehavior(GameObject *gameObject)

{
    ScriptComponent *scriptComponent = gameObject != nullptr ? gameObject->getComponent<ScriptComponent>() : nullptr;
    if (scriptComponent == nullptr)
    {
        return nullptr;
    }

    for (Behavior *behavior : scriptComponent->getBehaviors())
    {
        if (T *typedBehavior = dynamic_cast<T *>(behavior))
        {
            return typedBehavior;
        }
    }

    return nullptr;
}