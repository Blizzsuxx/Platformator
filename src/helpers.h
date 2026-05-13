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

inline int clipSegmentToLine(ClipVertex output[2], const ClipVertex input[2], const Eigen::Vector2f &normal, float offset, EdgeNumber clipEdge)
{
    int outputCount = 0;
    float distance0 = normal.dot(input[0].point) - offset;
    float distance1 = normal.dot(input[1].point) - offset;

    if (distance0 >= 0.0f)
    {
        output[outputCount++] = input[0];
    }
    if (distance1 >= 0.0f)
    {
        output[outputCount++] = input[1];
    }

    if (distance0 * distance1 < 0.0f)
    {
        float interpolation = distance0 / (distance0 - distance1);
        ContactFeature feature = distance0 < 0.0f ? input[0].feature : input[1].feature;

        if (distance0 < 0.0f)
        {
            feature.e.inEdge1 = static_cast<uint8_t>(clipEdge);
            feature.e.outEdge1 = static_cast<uint8_t>(NO_EDGE);
        }
        else
        {
            feature.e.inEdge1 = static_cast<uint8_t>(NO_EDGE);
            feature.e.outEdge1 = static_cast<uint8_t>(clipEdge);
        }

        output[outputCount++] = ClipVertex(input[0].point + interpolation * (input[1].point - input[0].point), feature);
    }

    return outputCount;
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

inline constexpr EdgeNumber getStartVertexAdjacentEdge(EdgeNumber edgeNumber)
{
    switch (edgeNumber)
    {
    case EDGE1:
        return EDGE2;
    case EDGE2:
        return EDGE3;
    case EDGE3:
        return EDGE4;
    case EDGE4:
        return EDGE1;
    case NO_EDGE:
    default:
        return NO_EDGE;
    }
}

inline constexpr EdgeNumber getEndVertexAdjacentEdge(EdgeNumber edgeNumber)
{
    switch (edgeNumber)
    {
    case EDGE1:
        return EDGE4;
    case EDGE2:
        return EDGE1;
    case EDGE3:
        return EDGE2;
    case EDGE4:
        return EDGE3;
    case NO_EDGE:
    default:
        return NO_EDGE;
    }
}