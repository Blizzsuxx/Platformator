#include "physicsmanager.h"
#include "runtimeaccess.h"
#include "runtime_work_probe.h"
#include "benchmarkbuildconfig.h"

#include <platformator/boxcollider.h>
#include <platformator/circlecollider.h>
#include <platformator/rigidbody.h>
#include <platformator/runtime.h>
#include <oneapi/tbb/global_control.h>
#include <json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#if PLATFORMATOR_ENABLE_WORK_PROBES
#include <atomic>
#include <new>
#if defined(__linux__)
#include <sys/resource.h>
#endif

namespace
{
std::atomic<bool> countAllocations{false};
std::atomic<size_t> allocationCount{0};
std::atomic<size_t> allocationBytes{0};
} // namespace

void *operator new(std::size_t bytes)
{
    if (countAllocations.load(std::memory_order_relaxed))
    {
        allocationCount.fetch_add(1, std::memory_order_relaxed);
        allocationBytes.fetch_add(bytes, std::memory_order_relaxed);
    }
    if (void *memory = std::malloc(bytes ? bytes : 1))
        return memory;
    throw std::bad_alloc();
}

void *operator new[](std::size_t bytes)
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

void operator delete(void *memory, std::size_t) noexcept
{
    std::free(memory);
}

void operator delete[](void *memory, std::size_t) noexcept
{
    std::free(memory);
}
#endif

namespace
{
using Clock = std::chrono::steady_clock;
using Json = nlohmann::json;
constexpr double frameDelta = 1.0 / 120.0;
constexpr double synchronizationDelta = frameDelta * 1e-6;
constexpr uint32_t workloadSeed = 1729;

void setHeadlessEnvironment()
{
#if defined(_WIN32)
    _putenv_s("SDL_AUDIODRIVER", "dummy");
    _putenv_s("SDL_VIDEODRIVER", "dummy");
    _putenv_s("SDL_RENDER_DRIVER", "software");
#else
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    setenv("SDL_RENDER_DRIVER", "software", 1);
#endif
}

RuntimeOptions headlessOptions()
{
    RuntimeOptions options;
    options.windowSettings.headless = true;
    return options;
}

Json baseRecord(const std::string &name, size_t workers)
{
    return Json{{"case", name},
                {"fixture_version", 1},
                {"seed", workloadSeed},
                {"workers", workers},
                {"compiler", PLATFORMATOR_BENCH_COMPILER},
                {"configuration", PLATFORMATOR_BENCH_CONFIGURATION},
                {"source_sha256", PLATFORMATOR_BENCH_SOURCE_SHA256}};
}

struct Tree
{
    GameObject *root = nullptr;
    std::vector<GameObject *> targets;
    size_t depth = 0;
    size_t breadth = 0;
};

Tree makeTree(platformator::Runtime &runtime, const std::string &shape)
{
    Tree tree;
    if (shape == "flat-small" || shape == "flat-large")
    {
        tree.breadth = shape == "flat-small" ? 32 : 256;
        tree.targets.reserve(tree.breadth);
        for (size_t index = 0; index < tree.breadth; ++index)
            tree.targets.push_back(runtime.createGameObject()->setPosition({static_cast<float>(index), 1}));
    }
    else if (shape == "deep-small" || shape == "deep-large")
    {
        tree.depth = shape == "deep-small" ? 16 : 64;
        tree.root = runtime.createGameObject()->setPosition({1, 1});
        auto *parent = tree.root;
        for (size_t index = 1; index < tree.depth; ++index)
        {
            auto *child = runtime.createGameObject()->setPosition({static_cast<float>(index + 1), 1});
            parent->addChild(child);
            parent = child;
        }
        tree.targets.push_back(parent);
    }
    else if (shape == "broad-small" || shape == "broad-large")
    {
        tree.depth = 2;
        tree.breadth = shape == "broad-small" ? 32 : 256;
        tree.root = runtime.createGameObject()->setPosition({1, 1});
        tree.targets.reserve(tree.breadth);
        for (size_t index = 0; index < tree.breadth; ++index)
        {
            auto *child = runtime.createGameObject()->setPosition({static_cast<float>(index + 2), 1});
            tree.root->addChild(child);
            tree.targets.push_back(child);
        }
    }
    else
    {
        throw std::invalid_argument("unknown hierarchy shape: " + shape);
    }
    runtime.simulateFrame(synchronizationDelta);
    return tree;
}

Json runRead(const std::string &shape, size_t workers)
{
    platformator::Runtime runtime(headlessOptions());
    const Tree tree = makeTree(runtime, shape);
    for (size_t index = 0; index < tree.targets.size(); ++index)
    {
        float expectedX = static_cast<float>(index);
        if (shape.starts_with("deep"))
            expectedX = static_cast<float>(tree.depth);
        else if (shape.starts_with("broad"))
            expectedX = static_cast<float>(index + 2);
        if (tree.targets[index]->getPosition() != Eigen::Vector2f(expectedX, 1))
            throw std::runtime_error("read fixture position differs from its expected hierarchy coordinates");
    }
    constexpr size_t reads = 100000;
    platformator_test::work = {};
    float checksum = 0;
    const auto start = Clock::now();
    for (size_t index = 0; index < reads; ++index)
    {
        const auto position = tree.targets[index % tree.targets.size()]->getPosition();
        checksum += position.x() * 0.25f + position.y();
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
    auto result = baseRecord("read-" + shape, workers);
    result["timing_boundary"] = "100000 immediate getPosition calls after fixture build and synchronization";
    result["operation_count"] = reads;
    result["target_count"] = tree.targets.size();
    result["depth"] = tree.depth;
    result["breadth"] = tree.breadth;
    result["elapsed_ns"] = elapsed;
    result["checksum"] = checksum;
#if PLATFORMATOR_ENABLE_WORK_PROBES
    result["position_read_visits"] = platformator_test::work.positionReadVisits;
#endif
    return result;
}

Json runAncestorEdit(size_t workers)
{
    platformator::Runtime runtime(headlessOptions());
    const Tree tree = makeTree(runtime, "deep-large");
    constexpr size_t edits = 1000;
    constexpr size_t readsPerEdit = 8;
    platformator_test::work = {};
    float checksum = 0;
    const auto start = Clock::now();
    for (size_t edit = 0; edit < edits; ++edit)
    {
        tree.root->setPosition({static_cast<float>(edit + 2), static_cast<float>(edit % 7)});
        for (size_t read = 0; read < readsPerEdit; ++read)
            checksum += tree.targets.front()->getPosition().x();
    }
    const Eigen::Vector2f expectedLeaf(static_cast<float>(edits + 1 + tree.depth - 1),
                                       static_cast<float>((edits - 1) % 7));
    if (tree.targets.front()->getPosition() != expectedLeaf)
        throw std::runtime_error("ancestor edit fixture produced an unexpected descendant position");
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
    auto result = baseRecord("ancestor-edit-deep", workers);
    result["timing_boundary"] = "1000 root edits, each followed by 8 immediate leaf reads";
    result["edits"] = edits;
    result["reads"] = edits * readsPerEdit;
    result["read_edit_ratio"] = readsPerEdit;
    result["depth"] = tree.depth;
    result["elapsed_ns"] = elapsed;
    result["checksum"] = checksum;
#if PLATFORMATOR_ENABLE_WORK_PROBES
    result["position_read_visits"] = platformator_test::work.positionReadVisits;
    result["immediate_position_refresh_visits"] = platformator_test::work.immediatePositionRefreshVisits;
#endif
    return result;
}

struct SetterPass
{
    int64_t elapsedNs = 0;
    bool boxQueued = false;
    bool circleQueued = false;
    bool bodySleeping = false;
    size_t colliderSyncRequests = 0;
    size_t bodyParticipationEnqueues = 0;
};

SetterPass runSetterPass(bool changed)
{
    platformator::Runtime runtime(headlessOptions());
    auto *boxObject = runtime.createGameObject()->addComponent<BoxCollider>(20, 30)->addComponent<Rigidbody>();
    auto *box = boxObject->getComponent<BoxCollider>();
    auto *body = boxObject->getComponent<Rigidbody>();
    body->setGravity(false);
    auto *circleObject = runtime.createGameObject()->setPosition({1000, 0})->addComponent<CircleCollider>(5);
    auto *circle = circleObject->getComponent<CircleCollider>();
    runtime.simulateFrame(frameDelta);
    body->setIsSleeping(true);
    runtime.simulateFrame(synchronizationDelta);

    constexpr size_t iterations = 10000;
    platformator_test::work = {};
    const auto start = Clock::now();
    for (size_t index = 0; index < iterations; ++index)
    {
        if (changed)
        {
            const bool odd = (index & 1) != 0;
            box->setWidth(odd ? 21.0f : 20.0f);
            box->setHeight(odd ? 31.0f : 30.0f);
            box->setOffset(odd ? Eigen::Vector2f(0.25f, -0.25f) : Eigen::Vector2f::Zero());
            circle->setRadius(odd ? 6.0f : 5.0f);
            circle->setOffset(odd ? Eigen::Vector2f(0.5f, 0.25f) : Eigen::Vector2f::Zero());
            body->setMass(odd ? 2.0f : 1.0f);
            body->setFriction(odd ? 0.5f : 1.0f);
            body->setRestitution(odd ? 0.2f : 0.0f);
            body->setGravity(odd);
        }
        else
        {
            box->setWidth(20.0f);
            box->setHeight(30.0f);
            box->setOffset(Eigen::Vector2f::Zero());
            circle->setRadius(5.0f);
            circle->setOffset(Eigen::Vector2f::Zero());
            body->setMass(1.0f);
            body->setFriction(1.0f);
            body->setRestitution(0.0f);
            body->setGravity(false);
        }
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
    if (changed && (box->getWidthWithoutScale() != 21.0f || box->getHeightWithoutScale() != 31.0f ||
                    circle->getRadius() != 6.0f || circle->getOffset() != Eigen::Vector2f(0.5f, 0.25f) ||
                    body->getMass() != 2.0f || body->getFriction() != 0.5f ||
                    body->getRestitution() != 0.2f || !body->getGravity()))
        throw std::runtime_error("changed-value setter control did not retain its final assignments");
    if (!changed && (box->getWidthWithoutScale() != 20.0f || box->getHeightWithoutScale() != 30.0f ||
                     circle->getRadius() != 5.0f || circle->getOffset() != Eigen::Vector2f::Zero() ||
                     body->getMass() != 1.0f || body->getFriction() != 1.0f ||
                     body->getRestitution() != 0.0f || body->getGravity()))
        throw std::runtime_error("same-value setter fixture changed its stored properties");
    SetterPass pass;
    pass.elapsedNs = elapsed;
    pass.boxQueued = box->getIsQueuedForSync();
    pass.circleQueued = circle->getIsQueuedForSync();
    pass.bodySleeping = body->getIsSleeping();
#if PLATFORMATOR_ENABLE_WORK_PROBES
    pass.colliderSyncRequests = platformator_test::work.colliderSyncRequests;
    pass.bodyParticipationEnqueues = platformator_test::work.bodyParticipationEnqueues;
#endif
    return pass;
}

Json runSetters(size_t workers)
{
    const auto unchanged = runSetterPass(false);
    const auto changed = runSetterPass(true);
    auto result = baseRecord("setters", workers);
    result["timing_boundary"] = "10000 iterations of box/circle geometry and body property assignments";
    result["iterations"] = 10000;
    result["properties_per_iteration"] = 9;
    result["unchanged"] = {{"elapsed_ns", unchanged.elapsedNs},
                           {"box_queued", unchanged.boxQueued},
                           {"circle_queued", unchanged.circleQueued},
                           {"body_sleeping", unchanged.bodySleeping},
                           {"collider_sync_requests", unchanged.colliderSyncRequests},
                           {"body_participation_enqueues", unchanged.bodyParticipationEnqueues}};
    result["changed_control"] = {{"elapsed_ns", changed.elapsedNs},
                                  {"box_queued", changed.boxQueued},
                                  {"circle_queued", changed.circleQueued},
                                  {"body_sleeping", changed.bodySleeping},
                                  {"collider_sync_requests", changed.colliderSyncRequests},
                                  {"body_participation_enqueues", changed.bodyParticipationEnqueues}};
    return result;
}

struct MovingFixture
{
    std::unique_ptr<platformator::Runtime> runtime;
    GameObject *parent = nullptr;
    GameObject *child = nullptr;
    PhysicsManager *physics = nullptr;
    std::vector<Rigidbody *> kinematicSetterBodies;
};

MovingFixture makeMovingFixture(size_t workers)
{
    (void)workers;
    MovingFixture fixture;
    fixture.runtime = std::make_unique<platformator::Runtime>(headlessOptions());
    fixture.parent = fixture.runtime->createGameObject()->setPosition({10, 0})->addComponent<Rigidbody>(KINEMATIC, false);
    fixture.child = fixture.runtime->createGameObject()->setPosition({30, 0})->addComponent<Rigidbody>(KINEMATIC, false);
    fixture.parent->addChild(fixture.child);
    fixture.parent->getComponent<Rigidbody>()->setVelocity({12, 0});
    fixture.child->getComponent<Rigidbody>()->setVelocity({24, 0});
    for (size_t index = 0; index < 64; ++index)
    {
        auto *dynamicObject = fixture.runtime->createGameObject()->setPosition({1000.0f + index * 4.0f, 0})
                                  ->addComponent<Rigidbody>(DYNAMIC, false);
        dynamicObject->getComponent<Rigidbody>()->setVelocity({static_cast<float>((index % 7) + 1), 0});
        auto *kinematicObject = fixture.runtime->createGameObject()->setPosition({2000.0f + index * 4.0f, 0})
                                    ->addComponent<Rigidbody>(KINEMATIC, false);
        auto *kinematicBody = kinematicObject->getComponent<Rigidbody>();
        kinematicBody->setVelocity({static_cast<float>((index % 5) + 1), 0});
        fixture.kinematicSetterBodies.push_back(kinematicBody);
    }
    fixture.runtime->simulateFrame(frameDelta);
    fixture.physics = platformator_detail::RuntimeAccess::physicsManager();
    return fixture;
}

Json runFrames(bool physicsOnlyScope, size_t workers)
{
    tbb::global_control concurrency(tbb::global_control::max_allowed_parallelism, workers);
    auto fixture = makeMovingFixture(workers);
    constexpr size_t warmupFrames = 5;
    constexpr size_t measuredFrames = 30;
    std::vector<int64_t> samples;
    samples.reserve(measuredFrames);
    const auto runOne = [&]() {
        if (!physicsOnlyScope)
        {
            fixture.runtime->simulateFrame(frameDelta);
            return;
        }
        fixture.physics->applyPhysics(frameDelta);
        fixture.physics->applyMovement(frameDelta);
        fixture.runtime->simulateFrame(synchronizationDelta);
    };
    for (size_t frame = 0; frame < warmupFrames; ++frame)
        runOne();
    for (size_t frame = 0; frame < measuredFrames; ++frame)
    {
        if (!physicsOnlyScope)
        {
            const auto start = Clock::now();
            fixture.runtime->simulateFrame(frameDelta);
            samples.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
            continue;
        }

        const auto start = Clock::now();
        fixture.physics->applyPhysics(frameDelta);
        fixture.physics->applyMovement(frameDelta);
        samples.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
        fixture.runtime->simulateFrame(synchronizationDelta);
    }
    const float expectedChildX = 30.0f + 36.0f * static_cast<float>(warmupFrames + measuredFrames + 1) *
                                                   static_cast<float>(frameDelta);
    if (std::abs(fixture.child->getX() - expectedChildX) > 0.02f)
        throw std::runtime_error("parent/child movement fixture produced an unexpected position");

    auto result = baseRecord(physicsOnlyScope ? "physics-integrate-move" : "complete-step", workers);
    result["timing_boundary"] = physicsOnlyScope
                                    ? "Clock covers PhysicsManager applyPhysics(frameDelta) + applyMovement(frameDelta); Runtime::simulateFrame(synchronizationDelta) follows untimed"
                                    : "Runtime::simulateFrame, full headless simulation step";
    result["warmup_frames"] = warmupFrames;
    result["measured_frames"] = measuredFrames;
    result["body_count"] = 130;
    result["shape"] = "parent-child kinematic pair plus 64 dynamic and 64 kinematic bodies";
    result["parent_child_expected_x"] = expectedChildX;
    result["parent_child_actual_x"] = fixture.child->getX();
    result["samples_ns"] = samples;
    return result;
}

struct MotionSetterPass
{
    int64_t elapsedNs = 0;
    size_t bodyParticipationEnqueues = 0;
    float finalVelocityX = 0;
    float finalAngularVelocity = 0;
};

MotionSetterPass runMotionSetterPass(bool changed)
{
    platformator::Runtime runtime(headlessOptions());
    auto *object = runtime.createGameObject()->addComponent<Rigidbody>(KINEMATIC, false);
    auto *body = object->getComponent<Rigidbody>();
    body->setVelocity({12, 0})->setAngularVelocity(3);
    runtime.simulateFrame(frameDelta);

    constexpr size_t iterations = 10000;
    platformator_test::work = {};
    const auto start = Clock::now();
    for (size_t iteration = 0; iteration < iterations; ++iteration)
    {
        const bool odd = (iteration & 1) != 0;
        body->setVelocity(changed && odd ? Eigen::Vector2f(24, 0) : Eigen::Vector2f(12, 0));
        body->setAngularVelocity(changed && odd ? 6.0f : 3.0f);
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();

    const float expectedVelocityX = changed ? 24.0f : 12.0f;
    const float expectedAngularVelocity = changed ? 6.0f : 3.0f;
    if (body->getVelocity().x() != expectedVelocityX || body->getAngularVelocity() != expectedAngularVelocity)
        throw std::runtime_error("motion setter fixture produced unexpected final values");

    MotionSetterPass pass;
    pass.elapsedNs = elapsed;
    pass.finalVelocityX = body->getVelocity().x();
    pass.finalAngularVelocity = body->getAngularVelocity();
#if PLATFORMATOR_ENABLE_WORK_PROBES
    pass.bodyParticipationEnqueues = platformator_test::work.bodyParticipationEnqueues;
#endif

    auto *sleepObject = runtime.createGameObject()->addComponent<Rigidbody>(DYNAMIC, false);
    auto *sleepBody = sleepObject->getComponent<Rigidbody>();
    runtime.simulateFrame(synchronizationDelta);
    sleepBody->setIsSleeping(true)->setVelocity({0, 0})->setAngularVelocity(0);
    const bool zeroAssignmentsKeepSleep = sleepBody->getIsSleeping();
    sleepBody->setVelocity({0.0001f, 0})->setAngularVelocity(0.00001f);
    const bool subthresholdAssignmentsKeepSleep = sleepBody->getIsSleeping();
    sleepBody->setVelocity({1, 0});
    const bool changedVelocityWakes = !sleepBody->getIsSleeping();
    sleepBody->setIsSleeping(true)->setAngularVelocity(1);
    const bool changedAngularVelocityWakes = !sleepBody->getIsSleeping();
    if (!zeroAssignmentsKeepSleep || !subthresholdAssignmentsKeepSleep || !changedVelocityWakes ||
        !changedAngularVelocityWakes)
        throw std::runtime_error("motion setter wake-threshold control produced an unexpected sleep state");
    return pass;
}

Json runMotionSetters(size_t workers)
{
    const auto unchanged = runMotionSetterPass(false);
    const auto changed = runMotionSetterPass(true);
    auto result = baseRecord("motion-setters", workers);
    result["timing_boundary"] = "10000 velocity/angular-velocity setter pairs per pass; fixture setup and wake-threshold checks outside timing";
    result["iterations"] = 10000;
    result["properties_per_iteration"] = 2;
    result["unchanged"] = {{"elapsed_ns", unchanged.elapsedNs},
                           {"body_participation_enqueues", unchanged.bodyParticipationEnqueues},
                           {"final_velocity_x", unchanged.finalVelocityX},
                           {"final_angular_velocity", unchanged.finalAngularVelocity}};
    result["changed_control"] = {{"elapsed_ns", changed.elapsedNs},
                                  {"body_participation_enqueues", changed.bodyParticipationEnqueues},
                                  {"final_velocity_x", changed.finalVelocityX},
                                  {"final_angular_velocity", changed.finalAngularVelocity}};
    result["wake_threshold_checks"] = {{"zero_assignments_keep_sleeping", true},
                                       {"subthreshold_assignments_keep_sleeping", true},
                                       {"changed_velocity_wakes", true},
                                       {"changed_angular_velocity_wakes", true}};
    return result;
}

struct SetterFramePass
{
    std::vector<int64_t> samplesNs;
    float expectedChildX = 0;
    float actualChildX = 0;
    float positionChecksum = 0;
    float stateChecksum = 0;
};

void assignStepMotionProperties(const MovingFixture &fixture, bool changed, size_t frame)
{
    const bool alternate = changed && (frame & 1) != 0;
    for (size_t index = 0; index < fixture.kinematicSetterBodies.size(); ++index)
    {
        const float baseVelocity = static_cast<float>((index % 5) + 1);
        fixture.kinematicSetterBodies[index]->setVelocity({alternate ? baseVelocity * 2.0f : baseVelocity, 0});
        fixture.kinematicSetterBodies[index]->setAngularVelocity(alternate ? 0.5f : 0.0f);
    }
}

SetterFramePass runSetterFramePass(size_t workers, bool changed)
{
    auto fixture = makeMovingFixture(workers);
    constexpr size_t warmupFrames = 5;
    constexpr size_t measuredFrames = 30;
    SetterFramePass pass;
    pass.samplesNs.reserve(measuredFrames);
    for (size_t frame = 0; frame < warmupFrames; ++frame)
    {
        assignStepMotionProperties(fixture, changed, frame);
        fixture.runtime->simulateFrame(frameDelta);
    }
    for (size_t frame = 0; frame < measuredFrames; ++frame)
    {
        const auto start = Clock::now();
        assignStepMotionProperties(fixture, changed, warmupFrames + frame);
        fixture.runtime->simulateFrame(frameDelta);
        pass.samplesNs.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
    }

    pass.expectedChildX = 30.0f + 36.0f * static_cast<float>(warmupFrames + measuredFrames + 1) *
                                           static_cast<float>(frameDelta);
    pass.actualChildX = fixture.child->getX();
    if (std::abs(pass.actualChildX - pass.expectedChildX) > 0.02f)
        throw std::runtime_error("setter step fixture produced an unexpected parent/child position");

    for (size_t index = 0; index < fixture.kinematicSetterBodies.size(); ++index)
    {
        const auto *body = fixture.kinematicSetterBodies[index];
        const float weight = static_cast<float>(index + 1);
        pass.positionChecksum += body->getGameObject()->getPosition().x() * weight;
        pass.stateChecksum += body->getVelocity().x() * weight + body->getAngularVelocity() * (weight + 1.0f);
    }
    return pass;
}

Json runSetterSteps(size_t workers)
{
    tbb::global_control concurrency(tbb::global_control::max_allowed_parallelism, workers);
    const auto unchanged = runSetterFramePass(workers, false);
    const auto changed = runSetterFramePass(workers, true);
    auto result = baseRecord("complete-step-setters", workers);
    result["timing_boundary"] = "host-call composite: 64 kinematic bodies x 2 velocity/angular-velocity setters followed by Runtime::simulateFrame; setters execute before the runtime call";
    result["warmup_frames"] = 5;
    result["measured_frames"] = 30;
    result["body_count"] = 130;
    result["setter_body_count"] = 64;
    result["setter_calls_per_frame"] = 128;
    result["shape"] = "parent-child kinematic pair plus 64 dynamic and 64 setter-driven kinematic bodies";
    result["unchanged_samples_ns"] = unchanged.samplesNs;
    result["changed_control_samples_ns"] = changed.samplesNs;
    result["unchanged_parent_child_expected_x"] = unchanged.expectedChildX;
    result["unchanged_parent_child_actual_x"] = unchanged.actualChildX;
    result["changed_parent_child_expected_x"] = changed.expectedChildX;
    result["changed_parent_child_actual_x"] = changed.actualChildX;
    result["unchanged_kinematic_position_checksum"] = unchanged.positionChecksum;
    result["changed_kinematic_position_checksum"] = changed.positionChecksum;
    result["unchanged_kinematic_state_checksum"] = unchanged.stateChecksum;
    result["changed_kinematic_state_checksum"] = changed.stateChecksum;
    return result;
}

#if PLATFORMATOR_ENABLE_WORK_PROBES
size_t peakRssKiB()
{
#if defined(__linux__)
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0)
        return static_cast<size_t>(usage.ru_maxrss);
#endif
    return 0;
}

Json runWork(size_t workers)
{
    tbb::global_control concurrency(tbb::global_control::max_allowed_parallelism, workers);
    Json result = baseRecord("work-and-resources", workers);
    result["timing_included"] = false;
    result["object_size_bytes"] = sizeof(GameObject);

    {
        constexpr size_t smallCount = 64;
        constexpr size_t largeCount = 1024;
        Json resourceRows = Json::array();
        for (const size_t count : {smallCount, largeCount})
        {
            platformator::Runtime runtime(headlessOptions());
            runtime.simulateFrame(synchronizationDelta);
            std::vector<GameObject *> objects;
            objects.reserve(count);
            allocationCount = 0;
            allocationBytes = 0;
            countAllocations = true;
            for (size_t index = 0; index < count; ++index)
                objects.push_back(runtime.createGameObject());
            countAllocations = false;
            resourceRows.push_back({{"object_count", count},
                                    {"object_storage_bytes", count * sizeof(GameObject)},
                                    {"fixture_allocation_count", allocationCount.load()},
                                    {"fixture_allocation_bytes", allocationBytes.load()},
                                    {"peak_rss_kib", peakRssKiB()}});
        }
        result["resource_growth"] = resourceRows;
    }

    {
        platformator::Runtime runtime(headlessOptions());
        const Tree tree = makeTree(runtime, "deep-large");
        platformator_test::work = {};
        for (size_t read = 0; read < 1000; ++read)
            (void)tree.targets.front()->getPosition();
        result["deep_reads"] = {{"reads", 1000},
                                {"depth", tree.depth},
                                {"position_read_visits", platformator_test::work.positionReadVisits}};
        platformator_test::work = {};
        for (size_t edit = 0; edit < 10; ++edit)
            tree.root->setPosition({static_cast<float>(edit + 2), 0});
        result["deep_ancestor_edits"] = {{"edits", 10},
                                         {"depth", tree.depth},
                                         {"immediate_position_refresh_visits",
                                          platformator_test::work.immediatePositionRefreshVisits}};
    }

    {
        platformator::Runtime runtime(headlessOptions());
        auto *boxObject = runtime.createGameObject()->addComponent<BoxCollider>(20, 30)->addComponent<Rigidbody>();
        auto *box = boxObject->getComponent<BoxCollider>();
        auto *body = boxObject->getComponent<Rigidbody>();
        body->setGravity(false);
        auto *circle = runtime.createGameObject()->setPosition({1000, 0})->addComponent<CircleCollider>(5)
                           ->getComponent<CircleCollider>();
        runtime.simulateFrame(frameDelta);
        body->setIsSleeping(true);
        runtime.simulateFrame(synchronizationDelta);
        platformator_test::work = {};
        for (size_t i = 0; i < 100; ++i)
        {
            box->setWidth(20);
            box->setHeight(30);
            box->setOffset(Eigen::Vector2f::Zero());
            circle->setRadius(5);
            circle->setOffset(Eigen::Vector2f::Zero());
            body->setMass(1);
            body->setFriction(1);
            body->setRestitution(0);
            body->setGravity(false);
        }
        result["unchanged_setters"] = {{"iterations", 100},
                                       {"collider_sync_requests", platformator_test::work.colliderSyncRequests},
                                       {"body_participation_enqueues",
                                        platformator_test::work.bodyParticipationEnqueues},
                                       {"box_queued", box->getIsQueuedForSync()},
                                       {"circle_queued", circle->getIsQueuedForSync()},
                                       {"body_sleeping", body->getIsSleeping()}};
    }

    {
        platformator::Runtime runtime(headlessOptions());
        auto *object = runtime.createGameObject()->addComponent<Rigidbody>(KINEMATIC, false);
        auto *body = object->getComponent<Rigidbody>();
        body->setVelocity({12, 0})->setAngularVelocity(3);
        runtime.simulateFrame(frameDelta);

        platformator_test::work = {};
        for (size_t iteration = 0; iteration < 100; ++iteration)
            body->setVelocity({12, 0})->setAngularVelocity(3);
        const size_t unchangedEnqueues = platformator_test::work.bodyParticipationEnqueues;
        runtime.simulateFrame(synchronizationDelta);

        platformator_test::work = {};
        for (size_t iteration = 0; iteration < 100; ++iteration)
        {
            const bool odd = (iteration & 1) != 0;
            body->setVelocity(odd ? Eigen::Vector2f(24, 0) : Eigen::Vector2f(12, 0))
                ->setAngularVelocity(odd ? 6.0f : 3.0f);
        }
        result["motion_setters"] = {{"iterations", 100},
                                    {"properties_per_iteration", 2},
                                    {"unchanged_body_participation_enqueues", unchangedEnqueues},
                                    {"changed_control_body_participation_enqueues",
                                     platformator_test::work.bodyParticipationEnqueues},
                                    {"final_velocity_x", body->getVelocity().x()},
                                    {"final_angular_velocity", body->getAngularVelocity()}};
    }

    return result;
}
#endif

} // namespace

int main(int argc, char **argv)
{
    setHeadlessEnvironment();
    std::string caseName;
    size_t workers = 1;
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (argument == "--case" && index + 1 < argc)
            caseName = argv[++index];
        else if (argument == "--workers" && index + 1 < argc)
            workers = static_cast<size_t>(std::stoul(argv[++index]));
        else
            throw std::invalid_argument("usage: platformator_transform_setter_probe --case NAME [--workers N]");
    }
    try
    {
        Json output;
        if (caseName == "read-flat-small")
            output = runRead("flat-small", workers);
        else if (caseName == "read-flat-large")
            output = runRead("flat-large", workers);
        else if (caseName == "read-deep-small")
            output = runRead("deep-small", workers);
        else if (caseName == "read-deep-large")
            output = runRead("deep-large", workers);
        else if (caseName == "read-broad-small")
            output = runRead("broad-small", workers);
        else if (caseName == "read-broad-large")
            output = runRead("broad-large", workers);
        else if (caseName == "ancestor-edit-deep")
            output = runAncestorEdit(workers);
        else if (caseName == "setters")
            output = runSetters(workers);
        else if (caseName == "motion-setters")
            output = runMotionSetters(workers);
        else if (caseName == "complete-step-1")
            output = runFrames(false, 1);
        else if (caseName == "complete-step-6")
            output = runFrames(false, 6);
        else if (caseName == "complete-step-setters-1")
            output = runSetterSteps(1);
        else if (caseName == "complete-step-setters-6")
            output = runSetterSteps(6);
        else if (caseName == "physics-integrate-move-1")
            output = runFrames(true, 1);
        else if (caseName == "physics-integrate-move-6")
            output = runFrames(true, 6);
        else if (caseName == "work")
        {
#if PLATFORMATOR_ENABLE_WORK_PROBES
            output = runWork(workers);
#else
            throw std::runtime_error("work case requires PLATFORMATOR_ENABLE_WORK_PROBES=ON");
#endif
        }
        else
            throw std::invalid_argument("unknown case: " + caseName);
        std::cout << output.dump() << '\n';
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "transform setter probe failed: " << error.what() << '\n';
        return 1;
    }
}
