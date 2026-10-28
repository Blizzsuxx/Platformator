#include "box2d_tree_bridge.h"
#include "dynamic_tree.h"

int platformator_box2d_tree_create_proxy_marked(b2DynamicTree *tree, b2AABB bounds, uint64_t categoryBits,
                                                uint64_t userData)
{
    return b2CreateTreeProxyInternal(tree, bounds, categoryBits, userData, true);
}

void platformator_box2d_tree_move_proxy_marked(b2DynamicTree *tree, int proxyId, b2AABB bounds)
{
    b2DynamicTree_MoveProxyInternal(tree, proxyId, bounds, true);
}

int platformator_box2d_tree_rebuild(b2DynamicTree *tree, bool fullBuild)
{
    return b2DynamicTree_Rebuild(tree, fullBuild);
}
