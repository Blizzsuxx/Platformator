#include "box2d_tree_bridge.h"
#include "dynamic_tree.h"

int platformator_box2d_tree_rebuild(b2DynamicTree *tree)
{
    return b2DynamicTree_Rebuild(tree, true);
}
