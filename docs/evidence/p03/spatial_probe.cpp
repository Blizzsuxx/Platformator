// Isolated spatial-query probe. No proxies are attached to the live physics world.
// Adapter inheritance is required only by the existing Collider*-typed AABB API.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <vector>
#include <unsupported/Eigen/BVH>
#include <oneapi/tbb/global_control.h>
#include "aabb.h"
#include "grid.h"
#include "objectregistry.h"
#include "platformator/runtime.h"

using Box = Eigen::AlignedBox2f;
using Tree = Eigen::KdBVH<float, 2, size_t>;
using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point start)
{
    return std::chrono::duration<double, std::micro>(Clock::now() - start).count();
}
Box view(float x) { return {Eigen::Vector2f(x, 0), Eigen::Vector2f(x + 1280, 720)}; }

class BoundsAdapter : public Collider
{
public:
    size_t index;
    BoundsAdapter(GameObject *activeObject, size_t index, bool camera)
        : Collider(activeObject, ComponentType::COLLIDER, ColliderType::BoxCollider), index(index)
    {
        collisionGroup = camera ? 2 : 1;
        collisionMask = camera ? 1 : 2; // Only camera/sprite pairs, never sprite/sprite pairs.
    }
    void setBox(const Box &b)
    {
        xProjections.getMin()->setProjectedPosition(b.min().x());
        xProjections.getMax()->setProjectedPosition(b.max().x());
        yProjections.getMin()->setProjectedPosition(b.min().y());
        yProjections.getMax()->setProjectedPosition(b.max().y());
    }
    Eigen::Vector2f projectOntoAxis(const Eigen::Vector2f &) const override { return {}; }
    std::vector<Eigen::Vector2f> getNormals(const Collider *) const override { return {}; }
    Edge getEdgeWithNormal(const Eigen::Vector2f &) const override
    {
        return {Eigen::Vector2f::Zero(), Eigen::Vector2f::Zero(), Eigen::Vector2f::Zero(), NO_EDGE};
    }
protected:
    void updateCollider() override {}
};

class SeparateSAP
{
    platformator_detail::ObjectRegistry registry;
    std::vector<std::unique_ptr<BoundsAdapter>> proxies;
    Grid pairStorage;
    AABB intervals;
    BoundsAdapter camera;
public:
    std::vector<size_t> hits;
    explicit SeparateSAP(GameObject *activeObject)
        : pairStorage(nullptr), intervals(&pairStorage), camera(activeObject, SIZE_MAX, true) {}
    void build(GameObject *activeObject, const Tree::VolumeList &boxes)
    {
        registry.add(camera, platformator_detail::ObjectKind::Collider);
        for (size_t i = 0; i < boxes.size(); ++i)
        {
            auto proxy = std::make_unique<BoundsAdapter>(activeObject, i, false);
            registry.add(*proxy, platformator_detail::ObjectKind::Collider);
            proxy->setBox(boxes[i]);
            intervals.add(proxy.get());
            proxies.push_back(std::move(proxy));
        }
        camera.setBox(view(0));
        intervals.add(&camera);
        pairStorage.flushDeferredPairDeltas();
        hits.reserve(boxes.size());
    }
    void query(const Box &box, bool reinsert = false)
    {
        if (reinsert) intervals.remove(&camera);
        camera.setBox(box);
        if (reinsert) intervals.add(&camera);
        else intervals.repair(&camera);
        pairStorage.flushDeferredPairDeltas();
        hits.clear();
        // Keep this private sink's queue unconsumed: it tracks the current overlap set.
        // No narrow phase, contacts, solver, or gameplay callbacks ever run here.
        for (auto *pair : *pairStorage.getPendingNarrowPhasePairs())
        {
            auto *sprite = pair->getObjectA() == &camera ? pair->getObjectB() : pair->getObjectA();
            hits.push_back(static_cast<BoundsAdapter *>(sprite)->index);
        }
        std::sort(hits.begin(), hits.end());
    }
};

// The existing physics broad phase also partitions these interval lists into cells.
// This instance is independent; its owner is null and its proxies are unattached.
class SeparateGrid
{
    platformator_detail::ObjectRegistry registry;
    std::vector<std::unique_ptr<BoundsAdapter>> proxies;
    Grid grid;
    BoundsAdapter camera;
public:
    std::vector<size_t> hits;
    explicit SeparateGrid(GameObject *activeObject) : grid(nullptr), camera(activeObject, SIZE_MAX, true) {}
    void build(GameObject *activeObject, const Tree::VolumeList &boxes)
    {
        registry.add(camera, platformator_detail::ObjectKind::Collider);
        for (size_t i = 0; i < boxes.size(); ++i)
        {
            auto proxy = std::make_unique<BoundsAdapter>(activeObject, i, false);
            registry.add(*proxy, platformator_detail::ObjectKind::Collider);
            proxy->setBox(boxes[i]);
            grid.queueAddCollider(proxy.get());
            proxies.push_back(std::move(proxy));
        }
        camera.setBox(view(0));
        grid.queueAddCollider(&camera);
        grid.flushPendingCellUpdates();
        hits.reserve(boxes.size());
    }
    void query(const Box &box)
    {
        camera.setBox(box);
        camera.scheduleSync();
        grid.queueSyncCollider(&camera);
        grid.flushPendingCellUpdates();
        hits.clear();
        for (auto *pair : *grid.getPendingNarrowPhasePairs())
        {
            auto *sprite = pair->getObjectA() == &camera ? pair->getObjectB() : pair->getObjectA();
            hits.push_back(static_cast<BoundsAdapter *>(sprite)->index);
        }
        std::sort(hits.begin(), hits.end());
    }
};

struct BVHQuery
{
    const Tree::VolumeList &boxes;
    Box queryBox;
    std::vector<size_t> hits;
    bool intersectVolume(const Box &box) { return box.intersects(queryBox); }
    bool intersectObject(size_t i)
    {
        if (boxes[i].intersects(queryBox)) hits.push_back(i);
        return false;
    }
    void query(const Tree &tree, const Box &box)
    {
        queryBox = box;
        hits.clear();
        Eigen::BVIntersect(tree, *this);
        std::sort(hits.begin(), hits.end());
    }
};

int main()
{
    RuntimeOptions options;
    options.windowSettings.headless = true;
    options.assetRoot = "assets";
    platformator::Runtime runtime(options);
    oneapi::tbb::global_control workers(oneapi::tbb::global_control::max_allowed_parallelism, 1);
    auto *activeObject = runtime.createGameObject();
    std::puts("count,motion,mode,repetition,queries,mean_us,build_us,total_hits");
    for (size_t count : {size_t(1000), size_t(10000), size_t(100000)})
    {
        Tree::VolumeList boxes;
        for (size_t i = 0; i < count; ++i)
        {
            Eigen::Vector2f p(float(i % 400) * 32, float(i / 400) * 32);
            boxes.emplace_back(p, (p + Eigen::Vector2f(16, 24)).eval());
        }
        SeparateSAP sap(activeObject);
        auto start = Clock::now();
        sap.build(activeObject, boxes);
        double sapBuildUs = elapsed(start);
        SeparateGrid grid(activeObject);
        start = Clock::now();
        grid.build(activeObject, boxes);
        double gridBuildUs = elapsed(start);
        std::vector<size_t> ids(count);
        std::iota(ids.begin(), ids.end(), 0);
        Tree tree;
        start = Clock::now();
        tree.init(ids.begin(), ids.end(), boxes.begin(), boxes.end());
        double bvhBuildUs = elapsed(start);
        BVHQuery bvh{boxes, view(0), {}};
        bvh.hits.reserve(count);
        std::vector<size_t> flat;
        flat.reserve(count);
        auto flatQuery = [&](const Box &box) {
            flat.clear();
            for (size_t i = 0; i < boxes.size(); ++i)
                if (boxes[i].intersects(box)) flat.push_back(i);
        };
        for (float x : {0.f, 2.f, 100.f, 8000.f, 0.f})
        {
            auto box = view(x);
            flatQuery(box);
            sap.query(box);
            bvh.query(tree, box);
            grid.query(box);
            if (flat != sap.hits || flat != bvh.hits || flat != grid.hits)
                throw std::runtime_error("Spatial result mismatch");
            sap.query(box, true);
            if (flat != sap.hits) throw std::runtime_error("Reinsert result mismatch");
        }
        std::fprintf(stderr, "verified separate AABB/grid/BVH/flat count=%zu; sprite pairs filtered; workers=1\n", count);
        for (int motion = 0; motion < 3; ++motion)
        {
            int queries = motion == 2 ? 20 : 100;
            for (int rep = 0; rep < 5; ++rep)
            for (int offset = 0; offset < 5; ++offset)
            {
                const int mode = (rep + offset) % 5;
                sap.query(view(0));
                grid.query(view(0));
                uint64_t totalHits = 0;
                start = Clock::now();
                for (int i = 0; i < queries; ++i)
                {
                    auto box = view(motion == 0 ? 0.f : motion == 1 ? i * 2.f : i % 2 ? 8000.f : 0.f);
                    if (mode == 0) { flatQuery(box); totalHits += flat.size(); }
                    else if (mode == 1) { sap.query(box); totalHits += sap.hits.size(); }
                    else if (mode == 2) { bvh.query(tree, box); totalHits += bvh.hits.size(); }
                    else if (mode == 3) { sap.query(box, true); totalHits += sap.hits.size(); }
                    else { grid.query(box); totalHits += grid.hits.size(); }
                }
                double meanUs = elapsed(start) / queries;
                const char *names[] = {"cached_flat", "existing_separate_sap", "eigen_bvh", "sap_reinsert_camera", "existing_separate_grid"};
                const char *motions[] = {"stationary", "pan_2_units", "teleport_8000_units"};
                std::printf("%zu,%s,%s,%d,%d,%.3f,%.3f,%llu\n", count, motions[motion], names[mode], rep,
                            queries, meanUs, mode == 1 || mode == 3 ? sapBuildUs : mode == 2 ? bvhBuildUs : mode == 4 ? gridBuildUs : 0.,
                            static_cast<unsigned long long>(totalHits));
                std::fflush(stdout);
            }
        }
    }
}
