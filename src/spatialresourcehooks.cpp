#include "spatialresourcehooks.h"

#include <atomic>

namespace platformator_detail
{
namespace
{
std::atomic<bool> active{false};
thread_local SpatialResourceTag currentTag = SpatialResourceTag::Unclassified;
}

bool spatialResourceCaptureActive() noexcept
{
    return active.load(std::memory_order_relaxed);
}

void setSpatialResourceCaptureActive(bool value) noexcept
{
    active.store(value, std::memory_order_relaxed);
}

SpatialResourceTag spatialResourceCurrentTag() noexcept
{
    return currentTag;
}

SpatialResourceTag setSpatialResourceTag(SpatialResourceTag tag) noexcept
{
    const SpatialResourceTag previous = currentTag;
    currentTag = tag;
    return previous;
}

} // namespace platformator_detail
