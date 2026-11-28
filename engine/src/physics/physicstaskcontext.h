#pragma once

#include <oneapi/tbb/task.h>
#include <oneapi/tbb/task_group.h>

namespace platformator_detail
{
inline void prefetchRead(const void *ptr) noexcept
{
#if defined(__GNUC__) || defined(__clang__)
    __builtin_prefetch(ptr, 0, 1);
#else
    (void)ptr;
#endif
}

inline void prefetchWrite(const void *ptr) noexcept
{
#if defined(__GNUC__) || defined(__clang__)
    __builtin_prefetch(ptr, 1, 1);
#else
    (void)ptr;
#endif
}

// Prefetches both Cache Line 0 (filter/dispatch metadata) and Cache Line 1 (Collision storage) of a ColliderPair.
inline void prefetchColliderPair(const void *pair) noexcept
{
    prefetchRead(pair);
    prefetchRead(static_cast<const char *>(pair) + 64);
}

template <typename Work> void runPhysicsWork(const Work &work)
{
    if (tbb::task::current_context() == nullptr)
    {
        work();
        return;
    }

    // The owner thread commits these batches after the workers join. Caller cancellation cannot skip worker work.
    tbb::task_group_context context(tbb::task_group_context::isolated);
    tbb::task_group group(context);
    group.run_and_wait(work);
}
} // namespace platformator_detail
