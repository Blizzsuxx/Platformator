#include "dynamicaabbtree.h"
#include "box2d_tree_bridge.h"
#include "workprobe.h"
#include <box2d/constants.h>

#include <cmath>
#include <exception>
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
    if (double(bounds.maxX) - bounds.minX >= 100000.0 || double(bounds.maxY) - bounds.minY >= 100000.0)
        throw std::invalid_argument("Tree proxy extent must be strictly below 100,000 units");
    return box;
}

struct QueryCallbackState
{
    DynamicAabbTree::QueryCallback callback;
    void *context;
    std::exception_ptr exception;
};

bool queryCallbackTrampoline(DynamicAabbTree::ProxyId proxy, std::uint64_t userData, void *context) noexcept
{
    auto &state = *static_cast<QueryCallbackState *>(context);
    try
    {
        return state.callback(proxy, userData, state.context);
    }
    catch (...)
    {
        state.exception = std::current_exception();
        return false;
    }
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

DynamicAabbTree::ProxyId DynamicAabbTree::addMarked(const Bounds &bounds, std::uint64_t categoryBits,
                                                    std::uint64_t userData)
{
    return platformator_box2d_tree_create_proxy_marked(tree, toProxyBox(bounds), categoryBits, userData);
}

void DynamicAabbTree::remove(ProxyId proxy)
{
    b2DestroyTreeProxy(tree, proxy);
}

void DynamicAabbTree::update(ProxyId proxy, const Bounds &bounds)
{
    b2DynamicTree_MoveProxy(tree, proxy, toProxyBox(bounds));
}

void DynamicAabbTree::updateMarked(ProxyId proxy, const Bounds &bounds)
{
    platformator_box2d_tree_move_proxy_marked(tree, proxy, toProxyBox(bounds));
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
    QueryCallbackState state{callback, context, nullptr};
    const auto stats = b2DynamicTree_OverlapAABB(tree, toBox(bounds), maskBits, queryCallbackTrampoline, &state);
    if (state.exception)
        std::rethrow_exception(state.exception);
    return {stats.nodeVisits, stats.leafVisits};
}

int DynamicAabbTree::rebuild()
{
    return rebuild(MaintenancePolicy::FullRebuild).rebuildItemCount;
}

DynamicAabbTree::RebuildStats DynamicAabbTree::rebuild(MaintenancePolicy policy)
{
    const bool fullBuild = policy == MaintenancePolicy::FullRebuild;
    if (!fullBuild && policy != MaintenancePolicy::NativePartial)
        throw std::invalid_argument("Unknown dynamic tree maintenance policy");
    const int itemCount = platformator_box2d_tree_rebuild(tree, fullBuild);
    ++maintenanceStats.callCount;
    maintenanceStats.rebuildItemCount += itemCount;
    return {maintenanceStats.callCount, itemCount};
}

DynamicAabbTree::RebuildStats DynamicAabbTree::getMaintenanceStats() const
{
    return maintenanceStats;
}

DynamicAabbTree::Metrics DynamicAabbTree::getMetrics() const
{
    return {b2DynamicTree_GetProxyCount(tree), b2DynamicTree_GetHeight(tree), b2DynamicTree_GetByteCount(tree)};
}

float DynamicAabbTree::getQualityAreaRatio() const
{
    PLATFORMATOR_WORK_ADD(bvhQualityScans, 1);
    return b2DynamicTree_GetAreaRatio(tree);
}

void DynamicAabbTree::validate() const
{
    b2DynamicTree_Validate(tree);
}
} // namespace platformator_detail
