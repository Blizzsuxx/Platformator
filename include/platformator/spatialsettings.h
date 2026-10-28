#pragma once

enum class BroadPhaseMode
{
    Segmented,
    DynamicBvh
};

enum class RenderCullingMode
{
    None,
    Linear,
    DynamicBvh,
    Segmented
};

enum class BvhMaintenance
{
    NativePartial,
    FullRebuild
};

enum class BvhQueryExecution
{
    Serial,
    Parallel
};

struct SpatialSettings
{
    BroadPhaseMode broadPhase = BroadPhaseMode::Segmented;
    RenderCullingMode renderCulling = RenderCullingMode::None;
    BvhMaintenance bvhMaintenance = BvhMaintenance::NativePartial;
    float bvhPadding = 0.0f;
    BvhQueryExecution bvhQueryExecution = BvhQueryExecution::Parallel;
};
