#pragma once

#if PLATFORMATOR_ENABLE_WORK_PROBES
#include "../tests/runtime_work_probe.h"
#define PLATFORMATOR_WORK_CELL_SCOPE(name) ::platformator_test::CellOperationScope name
#define PLATFORMATOR_BVH_QUERY_SCOPE(name) ::platformator_test::SpatialQueryScope name
#define PLATFORMATOR_WORK_RENDER_SUBMISSION(windowIndex)                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        ::platformator_test::work.renderSpriteSubmissions++;                                                           \
        ::platformator_test::renderSubmittedWindowIndices.push_back(windowIndex);                                      \
    } while (false)
#else
#define PLATFORMATOR_WORK_ADD(field, count) ((void)0)
#define PLATFORMATOR_WORK_CELL_SCOPE(name) ((void)0)
#define PLATFORMATOR_BVH_QUERY_SCOPE(name) ((void)0)
#define PLATFORMATOR_WORK_RENDER_SUBMISSION(windowIndex) ((void)0)
#endif
