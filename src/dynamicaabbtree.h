#pragma once

#include <cstddef>
#include <cstdint>

struct b2DynamicTree;

namespace platformator_detail
{
// Private adapter shared by renderer and collider indexes. Bounds are in caller-selected
// coordinates; this class owns no engine objects. Mutations and maintenance are exclusive writers;
// callers may issue immutable queries only after publication and must join readers before writing.
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
        std::size_t memoryBytes;
    };
    enum class MaintenancePolicy
    {
        NativePartial,
        FullRebuild
    };
    struct RebuildStats
    {
        std::size_t callCount;
        int rebuildItemCount;
    };
    using ProxyId = int;
    // Return false to stop the query. Callbacks must not mutate the tree or throw.
    using QueryCallback = bool (*)(ProxyId, std::uint64_t userData, void *context);

    explicit DynamicAabbTree(int initialCapacity = 16);
    ~DynamicAabbTree();
    DynamicAabbTree(const DynamicAabbTree &) = delete;
    DynamicAabbTree &operator=(const DynamicAabbTree &) = delete;

    // Finite, ordered bounds; each proxy extent must be strictly below 100,000 units.
    ProxyId add(const Bounds &bounds, std::uint64_t categoryBits, std::uint64_t userData);
    // Internal adapter operations mark tree paths for the next NativePartial maintenance pass.
    ProxyId addMarked(const Bounds &bounds, std::uint64_t categoryBits, std::uint64_t userData);
    void remove(ProxyId proxy);
    void update(ProxyId proxy, const Bounds &bounds);
    void updateMarked(ProxyId proxy, const Bounds &bounds);
    void setCategoryBits(ProxyId proxy, std::uint64_t categoryBits);
    Bounds getBounds(ProxyId proxy) const;
    std::uint64_t getUserData(ProxyId proxy) const;
    QueryStats query(const Bounds &bounds, std::uint64_t maskBits, QueryCallback callback, void *context) const;
    int rebuild(); // Full rebuild; returns the number of boxes sorted.
    RebuildStats rebuild(MaintenancePolicy policy);
    RebuildStats getMaintenanceStats() const;
    Metrics getMetrics() const; // Constant-time gauges; safe in incremental/cached paths.
    float getQualityAreaRatio() const; // O(N), explicit untimed diagnostic only.
    void validate() const; // Heavy checks are enabled in Debug builds.

  private:
    b2DynamicTree *tree;
    RebuildStats maintenanceStats{0, 0};
};
} // namespace platformator_detail
