#include "physicsmanager.h"
#include "runtime_work_probe.h"
#include "runtimeaccess.h"
#include "renderculling.h"
#include "sdlwindow.h"
#include "test_support.h"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <new>
#include <oneapi/tbb/global_control.h>
#include <platformator/boxcollider.h>
#include <platformator/camera.h>
#include <platformator/rigidbody.h>
#include <platformator/scriptcomponent.h>
#include <platformator/sprite.h>

using namespace test_support;
#if PLATFORMATOR_ENABLE_WORK_PROBES
namespace
{
std::atomic<bool> countAllocations{false};
std::atomic<size_t> allocations{0};
} // namespace

void *operator new(size_t bytes)
{
    if (countAllocations.load(std::memory_order_relaxed))
    {
        allocations.fetch_add(1, std::memory_order_relaxed);
    }
    if (void *memory = std::malloc(bytes ? bytes : 1))
    {
        return memory;
    }
    throw std::bad_alloc();
}

void *operator new[](size_t bytes)
{
    return ::operator new(bytes);
}

void operator delete(void *memory) noexcept
{
    std::free(memory);
}

void operator delete[](void *memory) noexcept
{
    std::free(memory);
}

void operator delete(void *memory, size_t) noexcept
{
    std::free(memory);
}

void operator delete[](void *memory, size_t) noexcept
{
    std::free(memory);
}
#endif

#if PLATFORMATOR_ENABLE_WORK_PROBES
namespace
{
int runBvhWorkProbe(size_t workers)
{
    headlessEnvironment();
    try
    {
        tbb::global_control concurrency(tbb::global_control::max_allowed_parallelism, workers);
        RuntimeOptions options = physicsOnly();
        options.spatialSettings.broadPhase = BroadPhaseMode::DynamicBvh;
        options.spatialSettings.bvhMaintenance = BvhMaintenance::NativePartial;
        options.spatialSettings.bvhPadding = 8.0f;
        options.spatialSettings.bvhQueryExecution = BvhQueryExecution::Parallel;
        platformator::Runtime runtime(options);
        auto *physics = platformator_detail::RuntimeAccess::physicsManager();
        require(physics->getGridIfActive() == nullptr && physics->getSpatialIndexIfActive() != nullptr,
                "A BVH runtime owns only its selected spatial index and allocates no Grid");

        std::vector<GameObject *> objects;
        platformator_test::work = {};
        platformator_test::resetSpatialQueryConcurrency();
        runtime.simulateFrameWithCustomCallback(dt, [&](double) {
            for (int i = 0; i < 2048; ++i)
            {
                const float x = i == 0 ? 0.0f : (i == 1 ? 15.0f : 1000.0f + (i - 2) * 40.0f);
                auto *object = runtime.createGameObject()->setPosition({x, 0})->addComponent<BoxCollider>(20, 20);
                object->getComponent<BoxCollider>()->setIsTrigger(true);
                objects.push_back(object);
            }
        });
        auto *index = physics->getSpatialIndexIfActive();
        require(index->getProxyCount() == objects.size() && index->getRecordCapacity() == objects.size(),
                "Initial BVH publication owns one stable high-water record per collider");
        require(platformator_test::work.gridCellOperations == 0 && platformator_test::work.bvhTreeAdds == objects.size() &&
                    platformator_test::work.bvhMaintenancePasses == 1 &&
                    platformator_test::work.bvhQueryEndpoints == objects.size(),
                "Initial selected BVH work builds once and queries only its dirty endpoints");
        if (workers > 1)
            require(platformator_test::maxConcurrentSpatialQueries.load(std::memory_order_relaxed) > 1,
                    "Parallel BVH query work overlaps on the available multiworker configuration");
        require(index->getCandidatePairCount() == 1,
                "The control pair starts with one exact AABB candidate");

        runtime.simulateFrame(dt); // Confirm the initial exact candidate before idle accounting.
        platformator_test::work = {};
        allocations = 0;
        countAllocations = true;
        for (int step = 0; step < 60; ++step)
            runtime.simulateFrame(dt);
        countAllocations = false;
        const auto idle = platformator_test::work;
        require(idle.gridCellOperations == 0 && idle.bvhTreeAdds == 0 && idle.bvhTreeRemoves == 0 &&
                    idle.bvhTreeMoves == 0 && idle.bvhMaintenancePasses == 0 && idle.bvhQueryEndpoints == 0 &&
                    idle.bvhPairChecks == 0 && idle.bvhQualityScans == 0 && allocations == 0,
                "Unchanged BVH frames do no hidden Grid/index query work and allocate nothing after warmup");

        platformator_test::work = {};
        objects[0]->getComponent<BoxCollider>()->setWidth(8.0f);
        runtime.simulateFrame(dt);
        const auto contained = platformator_test::work;
        require(contained.bvhQueryEndpoints == 1 && contained.bvhContainedMoves == 1 && contained.bvhTreeMoves == 0 &&
                    contained.bvhMaintenancePasses == 0 && contained.bvhPairChecks > 0 &&
                    index->getCandidatePairCount() == 0,
                "Contained padded resize still queries and reconciles exact bounds without tree mutation");

        const size_t recordCapacity = index->getRecordCapacity();
        GameObject *retired = objects.back();
        platformator_test::work = {};
        runtime.simulateFrameWithCustomCallback(dt, [&](double) { retired->destroy(); });
        require(index->getProxyCount() == objects.size() - 1 &&
                    platformator_test::work.bvhRetiredRecords == 1,
                "Retirement removes the tree proxy and incident state before record reclamation");
        objects.pop_back();
        auto *replacement = runtime.createGameObject()->setPosition({90000, 0})->addComponent<BoxCollider>(20, 20);
        objects.push_back(replacement);
        runtime.simulateFrame(dt);
        require(index->getProxyCount() == objects.size() && index->getRecordCapacity() == recordCapacity,
                "A replacement reuses the retired stable record slot at the existing high-water mark");
        require(platformator_test::work.gridCellOperations == 0,
                "The inactive Grid remains untouched during BVH retirement and reuse");

        std::vector<GameObject *> dense;
        runtime.simulateFrameWithCustomCallback(dt, [&](double) {
            for (int i=0; i<64; ++i)
            {
                auto *object = runtime.createGameObject()->setPosition({-10000.0f + i*0.01f, 0})->addComponent<BoxCollider>(20,20);
                object->getComponent<BoxCollider>()->setIsTrigger(true);
                dense.push_back(object);
            }
        });
        runtime.simulateFrame(dt);
        platformator_test::work = {};
        runtime.simulateFrameWithCustomCallback(dt, [&](double) {
            for (auto *object : dense) object->setPosition(object->getPosition() + Eigen::Vector2f(0.1f,0));
        });
        require(platformator_test::work.pairAdjacencyQueueVisits <= 64*63,
                "Dense BVH publication queues adjacency once per dirty endpoint, not once per candidate pair");

        std::cout << "bvh proxies=" << index->getProxyCount() << " record_capacity=" << index->getRecordCapacity()
                  << " idle_allocations=" << allocations << " query_concurrency="
                  << platformator_test::maxConcurrentSpatialQueries.load(std::memory_order_relaxed) << '\n';
        return 0;
    }
    catch (const std::exception &error)
    {
        countAllocations = false;
        std::cerr << error.what() << '\n';
        return 1;
    }
}

RenderCullingMode parseRenderMode(const std::string &name)
{
    if (name == "none") return RenderCullingMode::None;
    if (name == "linear") return RenderCullingMode::Linear;
    if (name == "bvh") return RenderCullingMode::DynamicBvh;
    if (name == "segmented") return RenderCullingMode::Segmented;
    throw std::invalid_argument("Unknown render work mode: " + name);
}

int runRenderWorkProbe(RenderCullingMode mode, size_t workers)
{
    headlessEnvironment();
    try
    {
        {
        tbb::global_control concurrency(tbb::global_control::max_allowed_parallelism, workers);
        RuntimeOptions options;
        options.windowSettings.width = 320;
        options.windowSettings.height = 240;
        options.spatialSettings.renderCulling = mode;
        platformator::Runtime runtime(options);
        require(runtime.getRenderer() != nullptr, "Render work probe uses the software SDL renderer");
        auto *cameraObject = runtime.createGameObject();
        cameraObject->addComponent<Camera>(320.0f, 240.0f);
        auto *camera = cameraObject->getComponent<Camera>();
        std::vector<GameObject *> objects;
        std::vector<Sprite *> sprites;
        for (int i = 0; i < 1000; ++i)
        {
            const float x = i < 10 ? 16.0f + static_cast<float>(i) * 20.0f
                                   : 1000.0f + static_cast<float>(i - 10) * 12.0f;
            auto *object = runtime.createGameObject()->setPosition({x, 120.0f});
            object->addComponent<Sprite>();
            auto *sprite = object->getComponent<Sprite>();
            sprite->setWidth(8.0f);
            sprite->setHeight(8.0f);
            objects.push_back(object);
            sprites.push_back(sprite);
        }

        platformator_test::work = {};
        require(runtime.simulateAndRenderFrame(dt), "Render work probe publishes its initial sparse frame");
        const auto initial = platformator_detail::RuntimeAccess::window()->getRenderCullingStats();
        const bool direct = mode == RenderCullingMode::None;
        require(initial.submittedCount == (direct ? 1000 : 10),
                "None submits the sprite vector and each selected mode submits only ten visible sprites");
        require(initial.visibilityKnown == !direct && initial.visibleCount == (direct ? 0 : 10),
                "Visibility counts distinguish exact selected results from the none path's unmeasured control");
        require(platformator_test::work.renderBoundsPrepared == (direct ? 0 : 1000),
                "None performs no render-bound preparation while selected choices cache every sprite bound");
        require(platformator_test::work.renderIndexAdds ==
                    ((mode == RenderCullingMode::DynamicBvh || mode == RenderCullingMode::Segmented) ? 1000 : 0),
                "Only indexed selections allocate sprite visibility proxies");
        if (mode == RenderCullingMode::Linear)
            require(platformator_test::work.renderCandidateSortCalls == 0 &&
                        platformator_test::work.renderFullOrderWalks == 1000,
                    "Invalidated sparse linear frames refine the authoritative draw order without sorting candidates");

        platformator_test::work = {};
        require(runtime.simulateAndRenderFrame(dt), "Unchanged render frame reuses published visibility");
        const auto unchanged = platformator_detail::RuntimeAccess::window()->getRenderCullingStats();
        require(unchanged.resultCacheHit == !direct && platformator_test::work.renderResultCacheHits == (direct ? 0 : 1) &&
                    platformator_test::work.renderBoundsPrepared == 0 &&
                    platformator_test::work.renderVisibilityQueries == 0,
                "Unchanged selected frames reuse ordered results without cache, geometry or query work");

        platformator_test::work = {};
        sprites.front()->setWidth(9.0f);
        sprites.front()->setWidth(10.0f);
        sprites.front()->setWidth(11.0f);
        require(runtime.simulateAndRenderFrame(dt), "Coalesced sprite dimensions publish before submission");
        require(platformator_test::work.renderBoundsPrepared == (direct ? 0 : 1),
                "Repeated destination-size mutations coalesce to one prepared sprite bound");

        platformator_test::work = {};
        cameraObject->setPosition({10.0f, 0.0f});
        require(runtime.simulateAndRenderFrame(dt), "Camera translation publishes its current prepared view");
        const auto panned = platformator_detail::RuntimeAccess::window()->getRenderCullingStats();
        require(panned.viewRevision > initial.viewRevision && platformator_test::work.renderBoundsPrepared == 0 &&
                    (direct || platformator_test::work.renderVisibilityQueries == 1),
                "View translation invalidates results without rebuilding sprite extents");

        platformator_test::work = {};
        camera->setWidth(400.0f);
        require(runtime.simulateAndRenderFrame(dt), "Camera aspect ratio publishes current sprite extents");
        require(platformator_test::work.renderBoundsPrepared == (direct ? 0 : 1000),
                "A changed camera/output scale ratio refreshes all view-dependent sprite bounds");

        platformator_test::work = {};
        auto *added = runtime.createGameObject()->setPosition({80.0f, 120.0f});
        added->addComponent<Sprite>();
        added->getComponent<Sprite>()->setWidth(8.0f);
        added->getComponent<Sprite>()->setHeight(8.0f);
        require(runtime.simulateAndRenderFrame(dt), "Draw-order mutation invalidates and republishes visibility");
        const auto ordered = platformator_detail::RuntimeAccess::window()->getRenderCullingStats();
        require(ordered.drawOrderRevision > panned.drawOrderRevision &&
                    ordered.submittedCount == (direct ? 1001 : 11),
                "Sprite insertion invalidates cached order and preserves the current window draw vector");

        if (mode == RenderCullingMode::Linear)
        {
            for (auto *object : objects)
                object->setPosition({80.0f, 120.0f});
            platformator_test::work = {};
            platformator_test::renderSubmittedWindowIndices.clear();
            require(runtime.simulateAndRenderFrame(dt), "Invalidated full-visibility linear frame publishes in draw order");
            const auto full = platformator_detail::RuntimeAccess::window()->getRenderCullingStats();
            require(full.visibleCount == 1001 && full.candidateCount == 1001 && full.exactChecks == 1001 &&
                        platformator_test::work.renderCandidateSortCalls == 0 &&
                        platformator_test::renderSubmittedWindowIndices.size() == 1001,
                    "Invalidated full linear frames exact-check every unique input without candidate sorting");
            for (size_t index = 0; index < platformator_test::renderSubmittedWindowIndices.size(); ++index)
                require(platformator_test::renderSubmittedWindowIndices[index] == index,
                        "Linear output follows the authoritative window draw order at full visibility");

            platformator_test::work = {};
            require(runtime.simulateAndRenderFrame(dt), "Unchanged full-visibility linear frame reuses its result");
            const auto cachedFull = platformator_detail::RuntimeAccess::window()->getRenderCullingStats();
            require(cachedFull.resultCacheHit && platformator_test::work.renderResultCacheHits == 1 &&
                        platformator_test::work.renderCandidateSortCalls == 0 &&
                        platformator_test::work.renderBoundsPrepared == 0,
                    "The linear result cache still avoids candidate work for an unchanged frame");
        }

        platformator_test::work = {};
        cameraObject->removeComponent(CAMERA);
        require(runtime.simulateAndRenderFrame(dt), "No-camera frame exits before visibility maintenance");
        const auto noCamera = platformator_detail::RuntimeAccess::window()->getRenderCullingStats();
        require(noCamera.fallbackReason == "no-camera" && noCamera.visibilityQueries == 0 &&
                    noCamera.maintenanceOperations == 0 && platformator_test::work.renderBoundsPrepared == 0 &&
                    platformator_test::work.renderVisibilityQueries == 0,
                "No-camera frames do not prepare bounds or run selected visibility queries");
        runtime.clearScene();
    }

    {
        RuntimeOptions headlessOptions;
        headlessOptions.windowSettings.headless = true;
        headlessOptions.spatialSettings.renderCulling = mode;
        platformator::Runtime headless(headlessOptions);
        headless.createGameObject()->addComponent<Camera>(320.0f, 240.0f);
        for (int i = 0; i < 128; ++i)
        {
            auto *object = headless.createGameObject()->setPosition({static_cast<float>(i * 400), 0.0f});
            object->addComponent<Sprite>();
            object->getComponent<Sprite>()->setWidth(8.0f);
            object->getComponent<Sprite>()->setHeight(8.0f);
        }
        platformator_test::work = {};
        headless.simulateFrame(dt);
        require(headless.getRenderer() == nullptr && platformator_test::work.renderIndexAdds == 0 &&
                    platformator_test::work.renderBoundsPrepared == 0 &&
                    platformator_test::work.renderVisibilityQueries == 0,
                "Headless selected settings allocate and advance no render visibility state");
    }
    return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
} // namespace
#endif

int main(int argc, char **argv)
{
#if PLATFORMATOR_ENABLE_WORK_PROBES
    if (argc >= 2 && std::string(argv[1]) == "bvh")
        return runBvhWorkProbe(argc >= 3 ? std::stoul(argv[2]) : 1);
    if (argc >= 2 && std::string(argv[1]) == "render")
        return runRenderWorkProbe(parseRenderMode(argc >= 3 ? argv[2] : "linear"), argc >= 4 ? std::stoul(argv[3]) : 1);
    headlessEnvironment();
    try
    {
        const size_t workers = argc == 2 ? std::stoul(argv[1]) : 1;
        tbb::global_control concurrency(tbb::global_control::max_allowed_parallelism, workers);
        platformator::Runtime runtime(physicsOnly());
        auto *root = runtime.createGameObject();
        for (int i = 0; i < 20; ++i)
        {
            root->addChild(runtime.createGameObject()->addComponent<BoxCollider>(2, 2));
        }
        for (int i = 0; i < 1000; ++i)
        {
            runtime.createGameObject()->setPosition({i * 40.f, 1000})->addComponent<Rigidbody>(STATIC, false);
            runtime.createGameObject()
                ->addComponent<Rigidbody>(DYNAMIC, false)
                ->getComponent<Rigidbody>()
                ->setIsSleeping(true);
        }
        std::vector<GameObject *> observed;
        for (int i = 0; i < 64; ++i)
        {
            auto *a = runtime.createGameObject()
                          ->setPosition({10000.f + i * 40.f, 0})
                          ->addComponent<BoxCollider>(4, 4);
            auto *b = runtime.createGameObject()
                          ->setPosition({10001.f + i * 40.f, 0})
                          ->addComponent<BoxCollider>(4, 4);
            a->getComponent<BoxCollider>()->setIsTrigger(true);
            a->addComponent<ScriptComponent>()->getComponent<ScriptComponent>()->addBehavior(
                new test_support::Behavior);
            observed.push_back(b);
        }
        for (int i = 0; i < 32; ++i)
        {
            runtime.simulateFrame(dt);
        }
        std::vector<GameObject *> cellWorkload;
        for (int i = 0; i < 128; ++i)
        {
            cellWorkload.push_back(runtime.createGameObject()
                ->setPosition({50000.f + i * 240.f, -10000.f})
                ->addComponent<BoxCollider>(8, 8));
        }
        platformator_test::work = {};
        platformator_test::resetCellConcurrency();
        runtime.simulateFrameWithCustomCallback(
            dt,
            [&](double) {
                for (int i = 0; i < static_cast<int>(cellWorkload.size()); ++i)
                {
                    cellWorkload[i]->setPosition({50000.f + i * 240.f, -8000.f});
                }
            });
        const size_t maxConcurrentCells =
            platformator_test::maxConcurrentCellOperations.load(std::memory_order_relaxed);
        require(platformator_test::work.gridCellOperations > 1 && maxConcurrentCells >= 1,
                "Segmented cell operations retain independent oneTBB work items after the shared-core refactor " +
                    std::to_string(platformator_test::work.gridCellOperations) + "/" +
                    std::to_string(maxConcurrentCells));
        if (workers > 1)
            require(maxConcurrentCells > 1, "Multiple allowed workers can process independent Grid cells concurrently");
        std::cout << "grid cell operations=" << platformator_test::work.gridCellOperations
                  << " max_concurrent=" << maxConcurrentCells << '\n';
        platformator_test::work = {};
        allocations = 0;
        countAllocations = true;
        for (int step = 0; step < 100; ++step)
        {
            runtime.simulateFrame(dt);
        }
        countAllocations = false;
        auto work = platformator_test::work;
        require(work.hierarchyRebuilds == 0 && work.transformVisits == 0 && work.reconciliations == 0 &&
                    work.geometryPairs == 0 && work.contactTransitionChecks == 0 &&
                    work.contactEventRecords == 0 && work.solverRefreshPairs == 0 && work.forceBodies == 0 &&
                    work.movingBodies == 0 && work.sleepBodies == 0,
                "Idle/static/unobserved contacts cannot trigger global lifecycle, transform, geometry, "
                "event, or body "
                "work");
        require(allocations == 0, "A warmed idle step must reuse its scratch allocations");
        std::cout << "idle allocations=" << allocations << " geometry=" << work.geometryPairs
                  << " transition_checks=" << work.contactTransitionChecks
                  << " event_records=" << work.contactEventRecords
                  << " solver_refresh_visits=" << work.solverRefreshPairs << '\n';
        platformator_test::work = {};
        runtime.simulateFrameWithCustomCallback(dt, [&](double) {
            root->setPosition({1, 0});
            root->setPosition({2, 0});
        });
        work = platformator_test::work;
        require(work.hierarchyRebuilds == 0 && work.transformVisits == 21,
                "One dirty root must visit only its affected subtree once");
        auto *changedChild = root->getChildren().front();
        auto *retiredChild = root->getChildren().back();
        platformator_test::work = {};
        runtime.simulateFrameWithCustomCallback(dt, [&](double) {
            root->setPosition({3, 0});
            changedChild->setPosition(changedChild->getPosition() + Eigen::Vector2f(1, 0));
            retiredChild->setPosition({100, 0});
            retiredChild->destroy();
        });
        require(platformator_test::work.transformVisits == 20,
                "Overlapping dirty parent/child publish once and cancel retired transform work");

        auto *victim = runtime.createGameObject()->addComponent<Rigidbody>(DYNAMIC, true);
        platformator_test::work = {};
        runtime.simulateFrameWithCustomCallback(dt, [&](double) {
            victim->destroy();
        });
        work = platformator_test::work;
        require(work.hierarchyRebuilds == 0 && work.forceBodies == 0 && work.movingBodies == 0,
                "Flat creation/retirement cannot rebuild the hierarchy or integrate a terminal body");
        platformator_test::work = {};
        auto *script = observed.front()->addComponent<ScriptComponent>()->getComponent<ScriptComponent>();
        script->addBehavior(new test_support::Behavior);
        runtime.simulateFrame(dt);
        observed.front()->removeComponent(SCRIPT);
        observed.front()->addComponent<ScriptComponent>()->getComponent<ScriptComponent>()->addBehavior(
            new test_support::Behavior);
        runtime.simulateFrame(dt);
        work = platformator_test::work;
        require(work.solverRefreshPairs == 0 && work.geometryPairs == 0 &&
                    work.contactTransitionChecks == 0 && work.contactEventRecords == 0,
                "Script startup and replacement must not visit physical adjacency or contact transitions");
        observed.front()->addComponent<Rigidbody>(DYNAMIC, false);
        runtime.simulateFrame(dt);
        observed.front()->getComponent<Rigidbody>()->setIsSleeping(true);
        platformator_test::work = {};
        runtime.simulateFrame(dt);
        require(platformator_test::work.solverRefreshPairs > 0,
                "Body sleep transitions still reconcile actual physical adjacency");
        auto *physics = platformator_detail::RuntimeAccess::physicsManager();
        std::pair<size_t, size_t> retained;
        auto *supported = runtime.createGameObject()
                              ->setPosition({40000, 0})
                              ->addComponent<BoxCollider>(20, 20)
                              ->addComponent<Rigidbody>(KINEMATIC, false);
        runtime.createGameObject()
            ->setPosition({40000, 19})
            ->addComponent<BoxCollider>(40, 20)
            ->addComponent<Rigidbody>(STATIC, false);
        runtime.simulateFrame(dt);
        runtime.simulateFrame(dt);
        require(supported->getComponent<Rigidbody>()->getSupportContactCount() == 1,
                "Churn body-replacement control has a real physical support");
        const auto originalContacts = physics->getContactPairs().size();
        for (int cycle = 0; cycle < 5; ++cycle)
        {
            std::vector<GameObject *> churn;
            runtime.simulateFrameWithCustomCallback(dt, [&](double) {
                supported->removeComponent(RIGID_BODY);
                supported->addComponent<Rigidbody>(KINEMATIC, false);
                auto *cancelled = runtime.createGameObject()->addComponent<Rigidbody>(KINEMATIC, false);
                cancelled->getComponent<Rigidbody>()->setVelocity({10, 0});
                cancelled->destroy();
                for (int i = 0; i < 16; ++i)
                {
                    for (int endpoint = 0; endpoint < 2; ++endpoint)
                    {
                        auto *object = runtime.createGameObject()
                                           ->setPosition({30000.f + i * 40.f + endpoint, 0})
                                           ->addComponent<BoxCollider>(4, 4);
                        object->getComponent<BoxCollider>()->setIsTrigger(true);
                        churn.push_back(object);
                    }
                }
            });
            runtime.simulateFrame(
                dt); // Components created in the hook register at cleanup; confirm geometry next step.
            require(supported->getComponent<Rigidbody>()->getSupportContactCount() == 1,
                    "Fixed-peak replacement restores exactly one physical support to the new recipient");
            require(physics->getContactPairs().size() == originalContacts + 16,
                    "Each churn cycle must add sixteen real confirmed contacts");
            const auto capacity = std::pair{physics->getPendingPhysicsEvents().capacity(),
                                            physics->getContactPairs().capacity()};
            if (cycle == 0)
            {
                retained = capacity;
            }
            else
            {
                require(capacity == retained,
                        "Fixed-peak contact churn must reuse retained event/contact storage");
            }
            runtime.simulateFrameWithCustomCallback(dt, [&](double) {
                for (auto *object : churn)
                {
                    object->destroy();
                }
            });
            require(physics->getContactPairs().size() == originalContacts,
                    "Churn removals must restore contact membership");
        }
        std::cout << "fixed-peak churn retained event_slots=" << retained.first
                  << " contact_slots=" << retained.second << '\n';
        std::cout << "[PASS] affected-work counters workers=" << workers << "\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
#else
    std::cout << "[SKIP] configure PLATFORMATOR_ENABLE_WORK_PROBES=ON for the work oracle\n";
    return 77;
#endif
}
