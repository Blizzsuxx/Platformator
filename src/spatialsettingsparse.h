#pragma once

#include <charconv>
#include <cmath>
#include <stdexcept>
#include <string>

#include "platformator/spatialsettings.h"

namespace platformator_detail
{
inline bool isSpatialSettingsOption(const std::string &argument)
{
    return argument == "--broad-phase" || argument == "--render-culling" || argument == "--bvh-maintenance" ||
           argument == "--bvh-padding" || argument == "--bvh-query-execution";
}

inline void parseSpatialSettingsOption(const std::string &argument, const std::string &value,
                                       SpatialSettings &settings)
{
    if (argument == "--broad-phase")
    {
        if (value == "segmented")
            settings.broadPhase = BroadPhaseMode::Segmented;
        else if (value == "bvh")
            settings.broadPhase = BroadPhaseMode::DynamicBvh;
        else
            throw std::runtime_error("--broad-phase must be segmented or bvh");
        return;
    }
    if (argument == "--render-culling")
    {
        if (value == "none")
            settings.renderCulling = RenderCullingMode::None;
        else if (value == "linear")
            settings.renderCulling = RenderCullingMode::Linear;
        else if (value == "bvh")
            settings.renderCulling = RenderCullingMode::DynamicBvh;
        else if (value == "segmented")
            settings.renderCulling = RenderCullingMode::Segmented;
        else
            throw std::runtime_error("--render-culling must be none, linear, bvh or segmented");
        return;
    }
    if (argument == "--bvh-maintenance")
    {
        if (value == "native-partial")
            settings.bvhMaintenance = BvhMaintenance::NativePartial;
        else if (value == "full-rebuild")
            settings.bvhMaintenance = BvhMaintenance::FullRebuild;
        else
            throw std::runtime_error("--bvh-maintenance must be native-partial or full-rebuild");
        return;
    }
    if (argument == "--bvh-query-execution")
    {
        if (value == "serial")
            settings.bvhQueryExecution = BvhQueryExecution::Serial;
        else if (value == "parallel")
            settings.bvhQueryExecution = BvhQueryExecution::Parallel;
        else
            throw std::runtime_error("--bvh-query-execution must be serial or parallel");
        return;
    }
    if (argument == "--bvh-padding")
    {
        float parsedValue = 0.0f;
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), parsedValue,
                                            std::chars_format::general);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
            !std::isfinite(parsedValue) || parsedValue < 0.0f)
            throw std::runtime_error("--bvh-padding must be finite and nonnegative");
        settings.bvhPadding = parsedValue;
        return;
    }
    throw std::runtime_error("Unknown spatial option: " + argument);
}
} // namespace platformator_detail
