#include "dynamicaabbtree.h"
#include "box2d_tree_bridge.h"
#include <box2d/constants.h>

#include <cmath>
#include <stdexcept>

namespace platformator_detail
{
namespace
{
b2AABB toBox(const DynamicAabbTree::Bounds &bounds)
{
    if (!std::isfinite(bounds.minX) || !std::isfinite(bounds.minY) || !std::isfinite(bounds.maxX) ||
        !std::isfinite(bounds.maxY) || bounds.minX > bounds.maxX || bounds.minY > bounds.maxY)
        throw std::invalid_argument("Tree bounds must be finite and ordered");
    return {{bounds.minX, bounds.minY}, {bounds.maxX, bounds.maxY}};
}

b2AABB toProxyBox(const DynamicAabbTree::Bounds &bounds)
{
    const auto box = toBox(bounds);
    // Match the upstream move-operation limits before crossing its assertion boundary.
    if (double(bounds.maxX) - bounds.minX >= B2_HUGE || double(bounds.maxY) - bounds.minY >= B2_HUGE)
        throw std::invalid_argument("Tree proxy extent exceeds Box2D's supported range");
    return box;
}
} // namespace

DynamicAabbTree::DynamicAabbTree(int initialCapacity) : tree(nullptr)
{
    if (initialCapacity < 0)
        throw std::invalid_argument("Tree capacity must be nonnegative");
    tree = b2CreateDynamicTree(initialCapacity);
}

DynamicAabbTree::~DynamicAabbTree()
{
    b2DestroyDynamicTree(tree);
}

DynamicAabbTree::ProxyId DynamicAabbTree::add(const Bounds &bounds, std::uint64_t categoryBits,
                                            std::uint64_t userData)
{
    return b2CreateTreeProxy(tree, toProxyBox(bounds), categoryBits, userData);
}

void DynamicAabbTree::remove(ProxyId proxy)
{
    b2DestroyTreeProxy(tree, proxy);
}

void DynamicAabbTree::update(ProxyId proxy, const Bounds &bounds)
{
    b2DynamicTree_MoveProxy(tree, proxy, toProxyBox(bounds));
}

void DynamicAabbTree::setCategoryBits(ProxyId proxy, std::uint64_t categoryBits)
{
    b2DynamicTree_SetCategoryBits(tree, proxy, categoryBits);
}

DynamicAabbTree::Bounds DynamicAabbTree::getBounds(ProxyId proxy) const
{
    const auto bounds = b2DynamicTree_GetProxyBounds(tree, proxy);
    return {bounds.lowerBound.x, bounds.lowerBound.y, bounds.upperBound.x, bounds.upperBound.y};
}

std::uint64_t DynamicAabbTree::getUserData(ProxyId proxy) const
{
    return b2DynamicTree_GetUserData(tree, proxy);
}

DynamicAabbTree::QueryStats DynamicAabbTree::query(const Bounds &bounds, std::uint64_t maskBits,
                                                 QueryCallback callback, void *context) const
{
    if (callback == nullptr)
        throw std::invalid_argument("Tree query callback must not be null");
    const auto stats = b2DynamicTree_OverlapAABB(tree, toBox(bounds), maskBits, callback, context);
    return {stats.nodeVisits, stats.leafVisits};
}

int DynamicAabbTree::rebuild()
{
    return platformator_box2d_tree_rebuild(tree);
}

DynamicAabbTree::Metrics DynamicAabbTree::getMetrics() const
{
    return {b2DynamicTree_GetProxyCount(tree), b2DynamicTree_GetHeight(tree),
            b2DynamicTree_GetAreaRatio(tree), b2DynamicTree_GetByteCount(tree)};
}

void DynamicAabbTree::validate() const
{
    b2DynamicTree_Validate(tree);
}
} // namespace platformator_detail
