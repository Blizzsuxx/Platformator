#pragma once

#if PLATFORMATOR_ENABLE_WORK_PROBES
#include "../tests/runtime_work_probe.h"
#else
#define PLATFORMATOR_WORK_ADD(field, count) ((void)0)
#endif
