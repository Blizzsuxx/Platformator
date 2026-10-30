#pragma once

#include <array>
#include <utility>

#include "document.h"

namespace platformator_editor
{
inline constexpr std::array<std::pair<int, const char *>, 7> kComponentChoices{{
    {0, "Animator"}, {1, "Audio"}, {2, "Camera"}, {3, "Collider"},
    {4, "Rigidbody"}, {5, "Script"}, {6, "Sprite"}}};

inline const char *component_label(int type) noexcept
{
    for (const auto &[candidate, label] : kComponentChoices)
        if (candidate == type)
            return label;
    return "Unknown Component";
}

inline bool has_component_type(const Json &object, int type) noexcept
{
    try
    {
        const auto components = object.find("components");
        if (components == object.end() || !components->is_array())
            return false;
        for (const auto &component : *components)
        {
            if (!component.is_object())
                continue;
            const auto componentType = component.find("type");
            if (componentType == component.end() ||
                (!componentType->is_number_integer() && !componentType->is_number_unsigned()))
                continue;
            if (componentType->get<int>() == type)
                return true;
        }
    }
    catch (...)
    {
    }
    return false;
}

inline Json default_component_record(int type)
{
    switch (type)
    {
    case 0:
        return Json{{"type", 0}, {"currentFrameIndex", 0}, {"playbackSpeed", 1.0},
                    {"playing", false}, {"animationClipFilePath", ""}};
    case 1:
        return Json{{"type", 1}, {"filePath", ""}, {"gain", 1.0}, {"loopCount", 0}, {"autoPlay", false}};
    case 2:
        return Json{{"type", 2}, {"width", 640.0}, {"height", 480.0}};
    case 3:
        return Json{{"type", 3}, {"colliderType", 1}, {"width", 16.0}, {"height", 16.0},
                    {"offset", {{"x", 0.0}, {"y", 0.0}}}, {"trigger", false},
                    {"collisionGroup", 1}, {"collisionMask", 1}};
    case 4:
        return Json{{"type", 4}, {"velocity", {{"x", 0.0}, {"y", 0.0}}},
                    {"force", {{"x", 0.0}, {"y", 0.0}}}, {"mass", 1.0},
                    {"angularVelocity", 0.0}, {"torque", 0.0}, {"friction", 0.5},
                    {"restitution", 0.0}, {"bodyType", 0}, {"gravity", true}};
    case 5:
        return Json{{"type", 5}, {"behaviors", Json::array()}};
    case 6:
        return Json{{"type", 6}, {"textureFilePath", ""}, {"flip", 0},
                    {"width", 16.0}, {"height", 16.0}, {"sourceRectEnabled", false}};
    default:
        return Json::object();
    }
}
} // namespace platformator_editor
