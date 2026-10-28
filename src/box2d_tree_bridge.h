#pragma once

#include <box2d/collision.h>

#ifdef __cplusplus
extern "C" {
#endif

int platformator_box2d_tree_rebuild(b2DynamicTree *tree);

#ifdef __cplusplus
}
#endif
