#include "physicsmanager.h"
#include "runtime_work_probe.h"
#include "runtimeaccess.h"
#include "test_support.h"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <new>
#include <oneapi/tbb/global_control.h>
#include <platformator/boxcollider.h>
#include <platformator/rigidbody.h>
#include <platformator/scriptcomponent.h>

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
Eigen::Vector2f runHeadlessCullingIsolation(bool callRenderedPath)
{
    headlessEnvironment();
    RuntimeOptions options = physicsOnly();
    options.windowSettings.enableRenderCulling = true;
    platformator::Runtime runtime(options);
    GameObject *object = runtime.createGameObject();
    for (int step = 1; step <= 8; ++step)
    {
        platformator_test::work = {};
        const auto update = [&, step](double) {
            object->setPosition({static_cast<float>(step * 3), -step * 2.0f});
        };
        if (callRenderedPath)
            require(runtime.simulateAndRenderFrameWithCustomCallback(dt, update),
                    "Headless simulate-and-render must still complete its simulation step");
        else
            runtime.simulateFrameWithCustomCallback(dt, update);

        const auto work = platformator_test::work;
        require(work.renderViewPreparations == 0 && work.renderEligibilityChecks == 0 &&
                    work.renderVisibilityChecks == 0 && work.renderSubmittedSprites == 0 &&
                    work.renderReadbackCallbacks == 0,
                "Headless simulation must do no view, eligibility, visibility, submission, or readback work");
    }
    return object->getPosition();
}

void headlessCullingIsolation()
{
    const Eigen::Vector2f simulationOnly = runHeadlessCullingIsolation(false);
    const Eigen::Vector2f simulateAndRender = runHeadlessCullingIsolation(true);
    require(simulationOnly.x() == simulateAndRender.x() && simulationOnly.y() == simulateAndRender.y(),
            "Enabling render culling cannot change headless simulation results");
    std::cout << "headless culling work=0/0/0/0/0 simulation_position=" << simulateAndRender.transpose() << '\n';
}
} // namespace
#endif

int main(int argc, char **argv)
{
#if PLATFORMATOR_ENABLE_WORK_PROBES
    headlessEnvironment();
    try
    {
        headlessCullingIsolation();
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
