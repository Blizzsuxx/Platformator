#include "dynamicaabbtree.h"
#include "workprobe.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

using Tree = platformator_detail::DynamicAabbTree;

namespace
{
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

bool collect(Tree::ProxyId, std::uint64_t data, void *context)
{
    static_cast<std::vector<std::uint64_t> *>(context)->push_back(data);
    return true;
}

std::vector<std::uint64_t> query(const Tree &tree, const Tree::Bounds &bounds, std::uint64_t mask = UINT64_MAX)
{
    std::vector<std::uint64_t> hits;
    tree.query(bounds, mask, collect, &hits);
    std::sort(hits.begin(), hits.end());
    return hits;
}

void cheapMetricsDoNotScan()
{
#if PLATFORMATOR_ENABLE_WORK_PROBES
    Tree tree;
    for (int i = 0; i < 1000; ++i) tree.addMarked({float(i*2),0,float(i*2+1),1},1,i);
    platformator_test::work = {};
    for (int i = 0; i < 20; ++i) require(tree.getMetrics().proxyCount == 1000, "Cheap gauges retain the population");
    require(platformator_test::work.bvhQualityScans == 0, "Routine metrics must not scan tree quality");
    require(tree.getQualityAreaRatio() > 0 && platformator_test::work.bvhQualityScans == 1,
            "Quality traversal is available only when explicitly requested");
#endif
}

void operations()
{
    Tree tree(1);
    const auto a = tree.add({0, 0, 1, 1}, 1, UINT64_C(0xfedcba9876543210));
    const auto b = tree.add({10, 10, 11, 11}, 2, 2);
    const Tree::Bounds view{-1, -1, 3, 3};
    require(query(tree, view).size() == 1, "Initial bounds query");
    tree.update(b, {1, 1, 2, 2});
    require(query(tree, view).size() == 2, "Moving proxy enters view");
    require(query(tree, view, 2) == std::vector<std::uint64_t>{2}, "Category filtering");
    tree.setCategoryBits(b, 4);
    require(query(tree, view, 2).empty(), "Changed categories affect queries");
    tree.update(a, {-20, -20, 20, 20});
    require(tree.getBounds(a).minX == -20 && tree.getBounds(a).maxY == 20, "Resized bounds retained");
    require(tree.getUserData(a) == UINT64_C(0xfedcba9876543210), "Opaque IDs retain all 64 bits");
    const auto before = query(tree, view);
    require(tree.rebuild() == 2 && query(tree, view) == before, "Rebuild preserves proxy IDs and results");
    tree.validate();
    require(tree.getMetrics().proxyCount == 2 && tree.getMetrics().memoryBytes > 0, "Metrics");
    int callbacks = 0;
    tree.query(view, UINT64_MAX, [](Tree::ProxyId, std::uint64_t, void *context) {
        ++*static_cast<int *>(context);
        return false;
    }, &callbacks);
    require(callbacks == 1, "Callback can stop traversal");
    tree.remove(a);
    require(query(tree, view) == std::vector<std::uint64_t>{2}, "Removed proxy disappears");
    tree.remove(b);
    require(tree.getMetrics().proxyCount == 0 && query(tree, view).empty(), "Empty tree query");
    tree.rebuild();
    tree.validate();
    const auto c = tree.add({0, 0, 1, 1}, 1, 3);
    require(query(tree, view) == std::vector<std::uint64_t>{3}, "Tree reused after removal and empty rebuild");
    tree.remove(c);
}

void invalidBounds()
{
    Tree tree;
    for (const auto &bounds : {Tree::Bounds{2, 0, 1, 1}, Tree::Bounds{0, 2, 1, 1},
                              Tree::Bounds{0, 0, std::numeric_limits<float>::infinity(), 1},
                              Tree::Bounds{0, 0, 100000, 1},
                              Tree::Bounds{0, 0, 1000000, 1},
                              Tree::Bounds{0, 0, std::numeric_limits<float>::quiet_NaN(), 1}})
    {
        bool rejected = false;
        try { tree.add(bounds, 1, 0); }
        catch (const std::invalid_argument &) { rejected = true; }
        require(rejected && tree.getMetrics().proxyCount == 0, "Invalid insertion leaves tree unchanged");
    }
    bool rejected = false;
    try { tree.query({0, 0, 1, 1}, UINT64_MAX, nullptr, nullptr); }
    catch (const std::invalid_argument &) { rejected = true; }
    require(rejected, "Null callback rejected before entering C code");
    const auto proxy = tree.add({0, 0, 1, 1}, 1, 7);
    rejected = false;
    try { tree.update(proxy, {0, 0, 1000000, 1}); }
    catch (const std::invalid_argument &) { rejected = true; }
    require(rejected && tree.getBounds(proxy).maxX == 1, "Invalid update preserves previous bounds");
    require(query(tree, {-1000000, -1000000, 1000000, 1000000}) == std::vector<std::uint64_t>{7},
            "Large query regions are allowed");
}

void movingBoundsOracle()
{
    struct Item { Tree::ProxyId proxy; Tree::Bounds bounds; std::uint64_t category; bool live; };
    Tree tree(1);
    std::mt19937 random(42);
    const auto position = [&] { return float(int(random() % 400) - 200); };
    const auto makeBounds = [&] {
        float x = position(), y = position();
        return Tree::Bounds{x, y, x + float(random() % 40 + 1), y + float(random() % 40 + 1)};
    };
    std::vector<Item> items;
    for (std::uint64_t i = 0; i < 96; ++i)
    {
        auto bounds = makeBounds();
        auto category = UINT64_C(1) << (i % 4);
        items.push_back({tree.add(bounds, category, i), bounds, category, true});
    }
    for (int step = 0; step < 80; ++step)
    {
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            auto &item = items[i];
            if (i % 7 == std::size_t(step % 7))
            {
                item.bounds = makeBounds();
                if (item.live) tree.update(item.proxy, item.bounds);
            }
            if (i % 13 == std::size_t(step % 13))
            {
                if (item.live) tree.remove(item.proxy);
                else item.proxy = tree.add(item.bounds, item.category, i);
                item.live = !item.live;
            }
            if (item.live && i % 11 == std::size_t(step % 11))
            {
                item.category = UINT64_C(1) << (random() % 4);
                tree.setCategoryBits(item.proxy, item.category);
            }
        }
        if (step % 9 == 0) tree.rebuild();
        tree.validate();
        for (int test = 0; test < 12; ++test)
        {
            auto bounds = makeBounds();
            std::uint64_t mask = random() % 16;
            std::vector<std::uint64_t> expected;
            for (std::size_t i = 0; i < items.size(); ++i)
            {
                const auto &item = items[i];
                const auto &b = item.bounds;
                if (item.live && (item.category & mask) && b.minX <= bounds.maxX && b.maxX >= bounds.minX &&
                    b.minY <= bounds.maxY && b.maxY >= bounds.minY)
                    expected.push_back(i);
            }
            require(query(tree, bounds, mask) == expected, "Moving/resizing/churn queries match brute force");
        }
    }
}

bool throwFromCallback(Tree::ProxyId, std::uint64_t, void *)
{
    throw std::runtime_error("query callback failure");
}

void markedMaintenanceAndReuse()
{
    using Policy = Tree::MaintenancePolicy;
    Tree tree(1);
    std::vector<Tree::ProxyId> proxies;
    for (uint64_t i = 0; i < 128; ++i)
    {
        const float x = static_cast<float>((i % 16) * 40);
        const float y = static_cast<float>((i / 16) * 40);
        proxies.push_back(tree.addMarked({x, y, x + 8, y + 8}, UINT64_C(1) << (i % 4), i));
    }
    const auto initial = tree.rebuild(Policy::NativePartial);
    require(initial.callCount == 1 && initial.rebuildItemCount > 0 && initial.rebuildItemCount <= 128,
            "Marked creation receives one native partial maintenance pass with actual item metrics");
    tree.validate();

    tree.updateMarked(proxies[37], {900, 900, 908, 908});
    const auto moved = tree.rebuild(Policy::NativePartial);
    require(moved.callCount == 2 && moved.rebuildItemCount > 0 && moved.rebuildItemCount <= 128,
            "A marked move rebuilds changed paths while retaining untouched subtrees");
    tree.validate();

    const auto beforeCategory = tree.getMaintenanceStats();
    tree.setCategoryBits(proxies[37], UINT64_C(1) << 63);
    const auto categoryOnly = tree.rebuild(Policy::NativePartial);
    require(categoryOnly.callCount == beforeCategory.callCount + 1 && categoryOnly.rebuildItemCount == 0,
            "Category-only changes update filtering without falsely marking spatial rebuild work");
    require(query(tree, {899, 899, 909, 909}, UINT64_C(1) << 63) == std::vector<std::uint64_t>{37},
            "Category changes are visible immediately to immutable queries");

    const auto full = tree.rebuild(Policy::FullRebuild);
    require(full.rebuildItemCount == 128, "Full rebuild reports the complete proxy population");
    tree.validate();

    const size_t highWaterBytes = tree.getMetrics().memoryBytes;
    for (int cycle = 0; cycle < 100; ++cycle)
    {
        const float x = static_cast<float>(cycle * 3);
        const auto proxy = tree.addMarked({x, -10, x + 1, -9}, 1, UINT64_C(1000) + cycle);
        tree.rebuild(Policy::NativePartial);
        tree.remove(proxy);
        tree.rebuild(Policy::NativePartial);
        require(tree.getMetrics().memoryBytes <= highWaterBytes,
                "Repeated churn reuses the high-water tree and rebuild capacity");
        tree.validate();
    }
    const auto maintenance = tree.getMaintenanceStats();
    require(maintenance.callCount == 204 && maintenance.rebuildItemCount >= 0,
            "Maintenance metrics count calls and actual returned build items");

    bool callbackThrew = false;
    try
    {
        tree.query({-20, -20, 1000, 1000}, UINT64_MAX, throwFromCallback, nullptr);
    }
    catch (const std::runtime_error &error)
    {
        callbackThrew = std::string(error.what()) == "query callback failure";
    }
    require(callbackThrew, "C callback trampoline reports C++ exceptions after the tree query returns");
}
} // namespace

int main()
{
    try
    {
        cheapMetricsDoNotScan();
        operations();
        invalidBounds();
        movingBoundsOracle();
        markedMaintenanceAndReuse();
        std::cout << "Dynamic AABB tree adapter passed\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
