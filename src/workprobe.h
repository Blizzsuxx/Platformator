#pragma once

#if PLATFORMATOR_ENABLE_WORK_PROBES
#include "../tests/runtime_work_probe.h"
#else
#define PLATFORMATOR_WORK_ADD(field, count) ((void)0)
#define PLATFORMATOR_WORK_SET(field, value) ((void)0)
#define PLATFORMATOR_RENDER_VIEW_PREPARED() ((void)0)
#define PLATFORMATOR_RENDER_ELIGIBILITY_CHECK() ((void)0)
#define PLATFORMATOR_RENDER_VISIBILITY_CHECK() ((void)0)
#define PLATFORMATOR_RENDER_SUBMISSION(sprite) ((void)0)
#define PLATFORMATOR_RENDER_BEFORE_PRESENT(renderer, width, height) ((void)0)
#define PLATFORMATOR_RENDER_CULLING_ADD(field, count) ((void)0)
#define PLATFORMATOR_RENDER_CULLING_SET(field, value) ((void)0)
#endif
