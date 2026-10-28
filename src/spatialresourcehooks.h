#pragma once

#include <cstdint>
#include <json.hpp>

namespace platformator_detail
{
enum class SpatialResourceTag : uint8_t
{
    Unclassified,
    PhysicsIndex,
    PhysicsCommonPairs,
    VisibilityIndex,
    Scratch,
    Box2D,
    Count
};

bool spatialResourceCaptureActive() noexcept;
void setSpatialResourceCaptureActive(bool active) noexcept;
SpatialResourceTag spatialResourceCurrentTag() noexcept;
SpatialResourceTag setSpatialResourceTag(SpatialResourceTag tag) noexcept;

class SpatialResourceTagScope
{
  public:
    explicit SpatialResourceTagScope(SpatialResourceTag tag) noexcept : previous(setSpatialResourceTag(tag)) {}
    ~SpatialResourceTagScope() { setSpatialResourceTag(previous); }
    SpatialResourceTagScope(const SpatialResourceTagScope &) = delete;
    SpatialResourceTagScope &operator=(const SpatialResourceTagScope &) = delete;

  private:
    SpatialResourceTag previous;
};

#if defined(PLATFORMATOR_RESOURCE_RUNNER)
void spatialResourceStart();
void spatialResourceMarkInitial();
void spatialResourceSetOwnedCapacity(SpatialResourceTag tag, uint64_t bytes);
// Defined by benchmark/spatial_resource_hooks.cpp in the resource-only executable.
nlohmann::json spatialResourceFinish();
#endif
} // namespace platformator_detail
