#pragma once

#include <cstddef>

namespace platformator_test
{
// Main-thread reductions only. Instrumented test builds opt in explicitly.
struct WorkCounters
{
    size_t hierarchyRebuilds = 0;
    size_t transformVisits = 0;
    size_t reconciliations = 0;
    size_t geometryPairs = 0;
    size_t stayPairs = 0;
    size_t forceBodies = 0;
    size_t movingBodies = 0;
    size_t sleepBodies = 0;
};
inline WorkCounters work;
} // namespace platformator_test

#if PLATFORMATOR_ENABLE_WORK_PROBES
#define PLATFORMATOR_WORK_ADD(field, count) (::platformator_test::work.field += (count))
#else
#define PLATFORMATOR_WORK_ADD(field, count) ((void)0)
#endif
