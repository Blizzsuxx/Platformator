#include "physicsmanager.h"
#include "runtimeaccess.h"
#include "test_support.h"

#include <oneapi/tbb/global_control.h>
#include <platformator/boxcollider.h>
#include <platformator/rigidbody.h>
#include <platformator/scriptcomponent.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using namespace test_support;

namespace
{
constexpr float PositionTolerance = 1.0e-4f;

struct EventRecorder : test_support::Behavior
{
    explicit EventRecorder(std::vector<std::string> &trace) : trace(trace) {}

    void onCollisionEnter(const Collision *collision, Collider *other, double) override
    {
        require(collision != nullptr && other != nullptr && other->getGameObject() != nullptr,
                "Confirmed Enter keeps both live endpoints available");
        trace.push_back("enter:" + getGameObject()->getName() + ">" + other->getGameObject()->getName());
    }

    void onCollisionExit(Collider *other, double) override
    {
        require(other != nullptr && other->getGameObject() != nullptr,
                "Exit keeps both endpoints available through dispatch");
        trace.push_back("exit:" + getGameObject()->getName() + ">" + other->getGameObject()->getName());
    }

    std::vector<std::string> &trace;
};

void attachRecorder(GameObject *object, std::vector<std::string> &trace)
{
    object->addComponent<ScriptComponent>()->getComponent<ScriptComponent>()->addBehavior(new EventRecorder(trace));
}

struct LifecycleResult
{
    std::vector<std::string> events;
    std::vector<std::string> checkpoints;
    Eigen::Vector2f finalPosition{0, 0};
};

LifecycleResult runContactTrace(BroadPhaseMode mode, size_t workers, BvhQueryExecution queryExecution, float padding)
{
    tbb::global_control concurrency(tbb::global_control::max_allowed_parallelism, workers);
    RuntimeOptions options = physicsOnly();
    options.spatialSettings.broadPhase = mode;
    options.spatialSettings.bvhQueryExecution = queryExecution;
    options.spatialSettings.bvhPadding = padding;
    platformator::Runtime runtime(options);
    require(runtime.getRenderer() == nullptr && runtime.getMainCamera() == nullptr,
            "Headless contact integration must have no camera or renderer");

    LifecycleResult result;
    auto *bodyObject = runtime.createGameObject()->setName("body")->addComponent<BoxCollider>(20, 20)
                           ->addComponent<Rigidbody>(DYNAMIC, false);
    auto *floorObject = runtime.createGameObject()->setName("floor")->setPosition({0, 19})
                            ->addComponent<BoxCollider>(40, 20)->addComponent<Rigidbody>(STATIC, false);
    auto *body = bodyObject->getComponent<Rigidbody>();
    auto *bodyCollider = bodyObject->getComponent<BoxCollider>();
    auto *floorCollider = floorObject->getComponent<BoxCollider>();
    attachRecorder(bodyObject, result.events);
    attachRecorder(floorObject, result.events);

    runtime.simulateFrame(dt);
    require(body->getSupportContactCount() == 1 &&
                platformator_detail::RuntimeAccess::physicsManager()->getContactPairs().size() == 1,
            "A confirmed off-screen contact contributes one persistent pair and one support");
    require(bodyCollider->getIsRegisteredInBroadPhase(), "Active collider is registered in selected broad phase");
    require(bodyCollider->getIsRegisteredInGrid() == (mode == BroadPhaseMode::Segmented),
            "Literal Grid membership reports the selected backend truthfully");
    require(result.events.size() == 2, "One confirmed contact delivers one Enter to each endpoint");
    result.checkpoints.push_back("enter:1/support:1");

    if (mode == BroadPhaseMode::DynamicBvh && padding > 0)
    {
        auto *nearMiss = runtime.createGameObject()->setName("padded-near-miss")->setPosition({0, -25})
                             ->addComponent<BoxCollider>(20, 20);
        nearMiss->getComponent<BoxCollider>()->setIsTrigger(true);
        runtime.simulateFrame(dt);
        require(platformator_detail::RuntimeAccess::physicsManager()->getContactPairs().size() == 1 &&
                    result.events.size() == 2,
                "Padded-envelope overlap is refined against exact bounds before creating a gameplay pair");
        result.checkpoints.push_back("padded-near-miss:no-contact");
    }

    body->setIsSleeping(true);
    floorCollider->setIsTrigger(true);
    runtime.simulateFrame(dt);
    require(!body->getIsSleeping() && body->getSupportContactCount() == 0,
            "Changing the last support to a trigger wakes a sleeping body and removes support");
    require(platformator_detail::RuntimeAccess::physicsManager()->getContactPairs().size() == 1,
            "Trigger response retains the geometric contact pair");
    require(result.events.size() == 2, "Response-only changes do not fabricate contact edges");
    result.checkpoints.push_back("trigger:awake/support:0");

    floorCollider->setIsTrigger(false);
    runtime.simulateFrame(dt);
    require(body->getSupportContactCount() == 1 && result.events.size() == 2,
            "Restoring solid response restores support without another Enter");
    floorCollider->setCollisionMask(0);
    runtime.simulateFrame(dt);
    require(body->getSupportContactCount() == 0 &&
                platformator_detail::RuntimeAccess::physicsManager()->getContactPairs().empty(),
            "Removing the final eligible pair clears contact and support state");
    require(result.events.size() == 4 && result.events[2].starts_with("exit:") &&
                result.events[3].starts_with("exit:"),
            "Last-pair loss dispatches exactly one Exit to each endpoint");
    result.checkpoints.push_back("filter-exit:2");

    floorCollider->setCollisionMask(UINT64_MAX);
    runtime.simulateFrame(dt);
    require(body->getSupportContactCount() == 1 && result.events.size() == 6,
            "Restoring a symmetric 64-bit filter creates one new contact edge");

    // Replacing the floor's body transfers response/support state without retaining the old body.
    floorObject->removeComponent(RIGID_BODY);
    runtime.simulateFrame(dt);
    require(body->getSupportContactCount() == 0, "Removing the supporting body drops its support contribution");
    floorObject->addComponent<Rigidbody>(STATIC, false);
    runtime.simulateFrame(dt);
    require(body->getSupportContactCount() == 1,
            "A replacement body receives support from the retained collider contact");
    result.checkpoints.push_back("body-replacement:support:1");

    // With no camera or renderer, moving out of range still produces a normal contact Exit.
    floorObject->setPosition({1000, 1000});
    runtime.simulateFrame(dt);
    require(platformator_detail::RuntimeAccess::physicsManager()->getContactPairs().empty() &&
                body->getSupportContactCount() == 0,
            "Headless physics continues broad-phase updates and removes off-screen contacts");
    floorObject->setPosition({0, 18});
    runtime.simulateFrame(dt);
    require(body->getSupportContactCount() == 1,
            "Headless physics recreates contact when the off-screen endpoint returns (support=" +
                std::to_string(body->getSupportContactCount()) + ", pairs=" +
                std::to_string(platformator_detail::RuntimeAccess::physicsManager()->getContactPairs().size()) +
                ", position=" + std::to_string(bodyObject->getX()) + "," +
                std::to_string(bodyObject->getY()) + ")");

    result.finalPosition = bodyObject->getPosition();
    require(std::isfinite(result.finalPosition.x()) && std::isfinite(result.finalPosition.y()),
            "Deterministic contact fixture retains finite numerical state");
    return result;
}

struct DestroyOnEnter : test_support::Behavior
{
    explicit DestroyOnEnter(int &enters, int &exits) : enters(enters), exits(exits) {}
    void onCollisionEnter(const Collision *, Collider *other, double) override
    {
        ++enters;
        other->getGameObject()->destroy();
    }
    void onCollisionExit(Collider *other, double) override
    {
        require(other != nullptr && other->getGameObject() != nullptr,
                "Callback-time retirement retains the Exit endpoint until delivery");
        ++exits;
    }
    int &enters;
    int &exits;
};

void callbackRetirement(BroadPhaseMode mode, size_t workers, BvhQueryExecution queryExecution, float padding)
{
    tbb::global_control concurrency(tbb::global_control::max_allowed_parallelism, workers);
    RuntimeOptions options = physicsOnly();
    options.spatialSettings.broadPhase = mode;
    options.spatialSettings.bvhQueryExecution = queryExecution;
    options.spatialSettings.bvhPadding = padding;
    platformator::Runtime runtime(options);
    int enters = 0, exits = 0;
    auto *source = runtime.createGameObject()->setName("source")->addComponent<BoxCollider>(20, 20);
    auto *victim = runtime.createGameObject()->setName("victim")->addComponent<BoxCollider>(20, 20);
    source->getComponent<BoxCollider>()->setIsTrigger(true);
    victim->getComponent<BoxCollider>()->setIsTrigger(true);
    source->addComponent<ScriptComponent>()->getComponent<ScriptComponent>()->addBehavior(
        new DestroyOnEnter(enters, exits));
    runtime.simulateFrame(dt);
    require(enters == 1 && exits == 1 && runtime.getGameObject("victim") == nullptr,
            "Destroying an endpoint during Enter drains its exact Exit before reclamation");
}

void unchangedPersistentContact(BroadPhaseMode mode, size_t workers, BvhQueryExecution queryExecution, float padding)
{
    tbb::global_control concurrency(tbb::global_control::max_allowed_parallelism, workers);
    RuntimeOptions options = physicsOnly();
    options.spatialSettings.broadPhase = mode;
    options.spatialSettings.bvhQueryExecution = queryExecution;
    options.spatialSettings.bvhPadding = padding;
    platformator::Runtime runtime(options);
    std::vector<std::string> events;
    auto *a = runtime.createGameObject()->setName("steady-a")->setPosition({800, 200})
                  ->addComponent<BoxCollider>(20, 20);
    auto *b = runtime.createGameObject()->setName("steady-b")->setPosition({800, 200})
                  ->addComponent<BoxCollider>(20, 20);
    a->getComponent<BoxCollider>()->setIsTrigger(true);
    b->getComponent<BoxCollider>()->setIsTrigger(true);
    attachRecorder(a, events);
    attachRecorder(b, events);
    runtime.simulateFrame(dt);
    require(platformator_detail::RuntimeAccess::physicsManager()->getContactPairs().size() == 1 && events.size() == 2,
            "Unchanged-contact fixture starts with one contact and two Enter callbacks");
    for (int frame = 0; frame < 16; ++frame)
        runtime.simulateFrame(dt);
    require(platformator_detail::RuntimeAccess::physicsManager()->getContactPairs().size() == 1 && events.size() == 2,
            "Unchanged persistent contact remains registered without duplicate callback transitions");
}

void sceneReplacementAndClear(BroadPhaseMode mode, size_t workers, BvhQueryExecution queryExecution, float padding)
{
    tbb::global_control concurrency(tbb::global_control::max_allowed_parallelism, workers);
    RuntimeOptions options = physicsOnly();
    options.spatialSettings.broadPhase = mode;
    options.spatialSettings.bvhQueryExecution = queryExecution;
    options.spatialSettings.bvhPadding = padding;
    platformator::Runtime runtime(options);
    auto *oldA = runtime.createGameObject()->setName("old-a")->addComponent<BoxCollider>(20, 20);
    auto *oldB = runtime.createGameObject()->setName("old-b")->addComponent<BoxCollider>(20, 20);
    runtime.simulateFrame(dt);
    require(platformator_detail::RuntimeAccess::physicsManager()->getContactPairs().size() == 1,
            "Scene fixture starts with one confirmed pair");

    const auto path = std::filesystem::absolute("spatial-physics-replacement.scene");
    writeScene(path, "replacement");
    runtime.loadScene(path.string());
    require(runtime.getGameObject("old-a") == nullptr && runtime.getGameObject("old-b") == nullptr &&
                runtime.getGameObject("replacement") != nullptr,
            "Scene replacement retires old collider endpoints before publishing new objects");
    runtime.getGameObject("replacement")->addComponent<BoxCollider>(12, 12);
    runtime.simulateFrame(dt);
    const auto &colliders = platformator_detail::RuntimeAccess::physicsManager()->getColliders();
    require(colliders.size() == 1 && colliders.front()->getIsRegisteredInBroadPhase() &&
                colliders.front()->getIsRegisteredInGrid() == (mode == BroadPhaseMode::Segmented),
            "Replacement scene registers only its new collider in the selected backend");
    runtime.clearScene();
    require(runtime.getAllGameObjects().empty() &&
                platformator_detail::RuntimeAccess::physicsManager()->getColliders().empty(),
            "Scene clear drains spatial membership and physics ownership");
    std::filesystem::remove(path);
    (void)oldA;
    (void)oldB;
}

void runMode(BroadPhaseMode mode, BvhQueryExecution queryExecution, float padding)
{
    const auto serial = runContactTrace(mode, 1, queryExecution, padding);
    const auto parallel = runContactTrace(mode, 6, queryExecution, padding);
    require(serial.events == parallel.events && serial.checkpoints == parallel.checkpoints,
            "Logical contact transitions are identical at one and six workers");
    require(std::abs(serial.finalPosition.x() - parallel.finalPosition.x()) <= PositionTolerance &&
                std::abs(serial.finalPosition.y() - parallel.finalPosition.y()) <= PositionTolerance,
            "Deterministic fixed-input fixture stays within the declared 1e-4 position tolerance");

    callbackRetirement(mode, 1, queryExecution, padding);
    callbackRetirement(mode, 6, queryExecution, padding);
    unchangedPersistentContact(mode, 1, queryExecution, padding);
    unchangedPersistentContact(mode, 6, queryExecution, padding);
    sceneReplacementAndClear(mode, 1, queryExecution, padding);
    sceneReplacementAndClear(mode, 6, queryExecution, padding);
}
} // namespace

int main()
{
    headlessEnvironment();
    try
    {
        runMode(BroadPhaseMode::Segmented, BvhQueryExecution::Serial, 0.0f);
        runMode(BroadPhaseMode::DynamicBvh, BvhQueryExecution::Serial, 0.0f);
        runMode(BroadPhaseMode::DynamicBvh, BvhQueryExecution::Parallel, 4.0f);
        std::cout << "Spatial physics integration regressions passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
