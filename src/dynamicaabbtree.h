#pragma once

#include <cstddef>
#include <cstdint>

struct b2DynamicTree;

namespace platformator_detail
{
// Private adapter shared by future renderer and collider prototypes.
// Bounds are in caller-selected coordinates; this class owns no engine objects.
class DynamicAabbTree
{
  public:
    struct Bounds
    {
        float minX, minY, maxX, maxY;
    };
    struct QueryStats
    {
        int nodeVisits, leafVisits;
    };
    struct Metrics
    {
        int proxyCount, height;
        float areaRatio;
        std::size_t memoryBytes;
    };
    using ProxyId = int;
    // Return false to stop the query. Callbacks must not mutate the tree or throw.
    using QueryCallback = bool (*)(ProxyId, std::uint64_t userData, void *context);

    explicit DynamicAabbTree(int initialCapacity = 16);
    ~DynamicAabbTree();
    DynamicAabbTree(const DynamicAabbTree &) = delete;
    DynamicAabbTree &operator=(const DynamicAabbTree &) = delete;

    // Finite, ordered bounds; proxy extents must be below Box2D's B2_HUGE limit.
    ProxyId add(const Bounds &bounds, std::uint64_t categoryBits, std::uint64_t userData);
    void remove(ProxyId proxy);
    void update(ProxyId proxy, const Bounds &bounds);
    void setCategoryBits(ProxyId proxy, std::uint64_t categoryBits);
    Bounds getBounds(ProxyId proxy) const;
    std::uint64_t getUserData(ProxyId proxy) const;
    QueryStats query(const Bounds &bounds, std::uint64_t maskBits, QueryCallback callback, void *context) const;
    int rebuild(); // Full rebuild; returns the number of boxes sorted.
    Metrics getMetrics() const;
    void validate() const; // Heavy checks are enabled in Debug builds.

  private:
    b2DynamicTree *tree;
};
} // namespace platformator_detail
