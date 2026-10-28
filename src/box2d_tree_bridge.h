#pragma once

#include <box2d/collision.h>

#ifdef __cplusplus
extern "C" {
#endif

int platformator_box2d_tree_create_proxy_marked(b2DynamicTree *tree, b2AABB bounds, uint64_t categoryBits,
                                                uint64_t userData);
void platformator_box2d_tree_move_proxy_marked(b2DynamicTree *tree, int proxyId, b2AABB bounds);
int platformator_box2d_tree_rebuild(b2DynamicTree *tree, bool fullBuild);

#ifdef __cplusplus
}
#endif
