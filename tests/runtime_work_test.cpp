#include "test_support.h"
#include "runtime_work_probe.h"
#include <iostream>
#include <atomic>
#include <cstdlib>
#include <new>
#include <oneapi/tbb/global_control.h>
#include <platformator/boxcollider.h>
#include <platformator/rigidbody.h>

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
        allocations.fetch_add(1, std::memory_order_relaxed);
    if (void *memory = std::malloc(bytes ? bytes : 1))
        return memory;
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
int main()
{
#if PLATFORMATOR_ENABLE_WORK_PROBES
    headlessEnvironment();
    try
    {
        tbb::global_control concurrency(tbb::global_control::max_allowed_parallelism, 1);
        platformator::Runtime runtime(physicsOnly());
        auto *root = runtime.createGameObject();
        for (int i = 0; i < 20; ++i)
            root->addChild(runtime.createGameObject()->addComponent<BoxCollider>(2, 2));
        for (int i = 0; i < 1000; ++i)
        {
            runtime.createGameObject()->setPosition({i * 40.f, 1000})->addComponent<Rigidbody>(STATIC, false);
            runtime.createGameObject()
                ->addComponent<Rigidbody>(DYNAMIC, false)
                ->getComponent<Rigidbody>()
                ->setIsSleeping(true);
        }
        for (int i = 0; i < 32; ++i)
            runtime.simulateFrame(dt);
        platformator_test::work = {};
        allocations = 0;
        countAllocations = true;
        runtime.simulateFrame(dt);
        countAllocations = false;
        auto work = platformator_test::work;
        require(
            work.hierarchyRebuilds == 0 && work.transformVisits == 0 && work.reconciliations == 0 &&
                work.geometryPairs == 0 && work.stayPairs == 0 && work.forceBodies == 0 && work.movingBodies == 0 &&
                work.sleepBodies == 0,
            "Idle/static/unobserved contacts cannot trigger global lifecycle, transform, geometry, stay, or body work");
        require(allocations == 0, "A warmed idle step must reuse its scratch allocations");
        platformator_test::work = {};
        runtime.simulateFrameWithCustomCallback(dt, [&](double) {
            root->setPosition({1, 0});
            root->setPosition({2, 0});
        });
        work = platformator_test::work;
        require(work.hierarchyRebuilds == 0 && work.transformVisits == 21,
                "One dirty root must visit only its affected subtree once");
        auto *victim = runtime.createGameObject()->addComponent<Rigidbody>(DYNAMIC, true);
        platformator_test::work = {};
        runtime.simulateFrameWithCustomCallback(dt, [&](double) { victim->destroy(); });
        work = platformator_test::work;
        require(work.hierarchyRebuilds == 0 && work.forceBodies == 0 && work.movingBodies == 0,
                "Flat creation/retirement cannot rebuild the hierarchy or integrate a terminal body");
        std::cout << "[PASS] affected-work counters\n";
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
