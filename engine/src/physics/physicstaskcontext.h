#pragma once

#include <oneapi/tbb/task.h>
#include <oneapi/tbb/task_group.h>

namespace platformator_detail
{
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
