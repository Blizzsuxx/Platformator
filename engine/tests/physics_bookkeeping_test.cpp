#include "core/gamemanager.h"
#include "physics/physicsmanager.h"
#include "core/runtimeaccess.h"
#include "test_support.h"
#if PLATFORMATOR_ENABLE_WORK_PROBES
#include "runtime_work_probe.h"
#endif
#include <algorithm>
#include <array>
#include <bit>
#include <functional>
#include <iostream>
#include <oneapi/tbb/global_control.h>
#include <oneapi/tbb/task.h>
#include <oneapi/tbb/task_arena.h>
#include <oneapi/tbb/task_group.h>
#include <platformator/boxcollider.h>
#include <platformator/circlecollider.h>
#include <platformator/rigidbody.h>
#include <unordered_set>
#include <vector>

using namespace test_support;

namespace
{
void baselineControls(size_t workers)
{
    tbb::global_control concurrency(tbb::global_control::max_allowed_parallelism, workers);
    platformator::Runtime runtime(physicsOnly());
    auto *physics = platformator_detail::RuntimeAccess::physicsManager();
    require(physics->getGrid().getPendingNarrowPhasePairs()->empty(), "Empty runtime has no pair work");
    auto *object = runtime.createGameObject()->addComponent<Rigidbody>(DYNAMIC, false);
    runtime.simulateFrame(dt);
    require(object->getComponent<Rigidbody>()->getIsRegisteredInPhysicsManager(),
            "The live body is registered before physical consumers run");
    require(object->getPosition().isZero(), "A zero-velocity gravity-free body stays at rest");
    require(physics->getGrid().getPendingNarrowPhasePairs()->empty(),
            "A body without shape has no contact work");
}

void queueLifecycle(size_t workers)
{
    tbb::global_control concurrency(tbb::global_control::max_allowed_parallelism, workers);
    platformator::Runtime runtime(physicsOnly());
    auto *physics = platformator_detail::RuntimeAccess::physicsManager();
    auto &manager = platformator_detail::RuntimeAccess::gameManager();
    const auto pending = [&]() -> const auto & {
        return *physics->getGrid().getPendingNarrowPhasePairs();
    };
    const auto verifyQueue = [&] {
        std::unordered_set<const ColliderPair *> distinct;
        for (size_t i = 0; i < pending().size(); ++i)
        {
            const auto *pair = pending()[i];
            require(pair->getIsQueuedForNarrowPhase() && pair->getNarrowPhaseQueueIndex() == i,
                    "Queued entries have their actual slot, including swap-repaired entries");
            require(distinct.insert(pair).second, "A pair is queued at most once");
        }
    };
    for (int i = 0; i < 131; ++i)
    {
        auto *a =
            runtime.createGameObject()->setPosition({float(i * 40), 0})->addComponent<BoxCollider>(4, 4);
        runtime.createGameObject()->setPosition({float(i * 40 + 1), 0})->addComponent<BoxCollider>(4, 4);
        a->getComponent<BoxCollider>()->setIsTrigger(true);
    }
    manager.commitMutations();
    require(pending().size() == 131, "The fixture has one independent queued pair per group");
    verifyQueue();
    auto *first = pending().front()->getObjectA()->getGameObject();
    for (int i = 0; i < 3; ++i)
    {
        first->setPosition({float(i % 2), 0});
        manager.commitMutations();
        require(pending().size() == 131, "Repeated sync does not duplicate pending pairs");
        verifyQueue();
    }
    for (int removal = 0; removal < 3; ++removal)
    {
        const auto before = pending().size();
        const size_t index = removal == 0 ? 0 : removal == 1 ? before / 2 : before - 1;
        pending()[index]->getObjectB()->setCollisionMask(0);
        manager.commitMutations();
        require(pending().size() == before - 1, "Excluded pre-evaluation pair is cancelled once");
        verifyQueue();
    }
    require(pending().size() == 128, "Parallel-path fixture retains 128 actual changed pairs");
    physics->checkForCollisions();
    require(pending().empty() && physics->getPendingPhysicsEvents().size() == 128,
            "Cancellation removes unconfirmed work and each surviving pair enters once");
    physics->handlePendingPhysicsEvents(dt);
    std::vector<GameObject *> endpoints;
    for (const auto *pair : physics->getContactPairs())
    {
        require(!pair->getIsQueuedForNarrowPhase() && pair->getNarrowPhaseQueueIndex() == SIZE_MAX,
                "Consumed entries retain no live queue membership");
        endpoints.push_back(pair->getObjectB()->getGameObject());
    }
    for (auto *endpoint : endpoints)
    {
        endpoint->setPosition(endpoint->getPosition() + Eigen::Vector2f(0.25f, 0));
    }
    manager.commitMutations();
    require(pending().size() == 128, "A consumed surviving pair can be queued again");
    verifyQueue();
    physics->checkForCollisions();
    require(pending().empty() && physics->getPendingPhysicsEvents().empty(),
            "Reevaluation of confirmed overlaps produces no duplicate Enter");
    physics->checkForCollisions();
    require(pending().empty(), "Empty evaluation batch is valid");
}

void contactMembershipRemoval(size_t workers)
{
    tbb::global_control concurrency(tbb::global_control::max_allowed_parallelism, workers);
    platformator::Runtime runtime(physicsOnly());
    auto *physics = platformator_detail::RuntimeAccess::physicsManager();
    auto &manager = platformator_detail::RuntimeAccess::gameManager();
    for (int i = 0; i < 5; ++i)
    {
        auto *a = runtime.createGameObject()->setPosition({float(i * 40), 0})->addComponent<BoxCollider>(4, 4);
        a->getComponent<BoxCollider>()->setIsTrigger(true);
        runtime.createGameObject()->setPosition({float(i * 40 + 1), 0})->addComponent<BoxCollider>(4, 4);
    }
    manager.commitMutations();
    physics->checkForCollisions();
    require(physics->getContactPairs().size() == 5, "Fixture starts with five independent contacts");
    for (int removal = 0; removal < 3; ++removal)
    {
        const auto &pairs = physics->getContactPairs();
        const size_t before = pairs.size();
        const size_t index = removal == 0 ? 0 : removal == 1 ? before / 2 : before - 1;
        pairs[index]->getObjectA()->setCollisionMask(0);
        manager.commitMutations();
        physics->checkForCollisions();
        require(physics->getContactPairs().size() == before - 1,
                "Removing first, middle and last contacts preserves all other memberships");
    }
}

void bodyMembershipRemoval(size_t workers)
{
    tbb::global_control concurrency(tbb::global_control::max_allowed_parallelism, workers);
    platformator::Runtime runtime(physicsOnly());
    std::vector<GameObject *> objects;
    for (int i = 0; i < 5; ++i)
    {
        auto *object = runtime.createGameObject()->addComponent<Rigidbody>(KINEMATIC, false);
        object->getComponent<Rigidbody>()->setVelocity({12, 0});
        objects.push_back(object);
    }
    runtime.simulateFrame(dt);
    for (int removal : {0, 2, 3})
    {
        objects[removal]->setActive(false);
        std::vector<float> before;
        for (auto *object : objects) before.push_back(object->getPosition().x());
        runtime.simulateFrame(dt);
        for (size_t i = 0; i < objects.size(); ++i)
        {
            const float expected = objects[i]->getActive() ? 0.1f : 0.0f;
            require(std::abs(objects[i]->getPosition().x() - before[i] - expected) < 1e-5f,
                    "Removing active body memberships preserves movement for all survivors");
        }
    }
}

void bodyParticipation(size_t workers)
{
    tbb::global_control concurrency(tbb::global_control::max_allowed_parallelism, workers);
    platformator::Runtime runtime(physicsOnly());
    auto *object = runtime.createGameObject()->addComponent<Rigidbody>(KINEMATIC, false);
    auto *body = object->getComponent<Rigidbody>();
    runtime.simulateFrame(dt);
    body->setVelocity({12, 0});
    runtime.simulateFrame(dt);
    require(std::abs(object->getPosition().x() - 0.1f) < 1e-5f,
            "Kinematic motion begins after zero velocity");
    body->setVelocity({0, 0})->setAngularVelocity(3);
    const auto position = object->getPosition();
    runtime.simulateFrame(dt);
    require((object->getPosition() - position).norm() < 1e-5f &&
                std::abs(object->getRotation() - 0.025f) < 1e-5f,
            "Angular-only kinematic motion remains eligible without translation");
    body->setAngularVelocity(0);
    const auto rotation = object->getRotation();
    runtime.simulateFrame(dt);
    require(object->getRotation() == rotation && object->getPosition() == position,
            "Stopping both kinematic velocities removes movement work");
    runtime.simulateFrameWithCustomCallback(dt, [&](double) {
        body->setBodyType(DYNAMIC)->setBodyType(STATIC)->setBodyType(KINEMATIC);
        body->setVelocity({24, 0})->setVelocity({0, 0})->setVelocity({24, 0});
    });
    require(std::abs(object->getPosition().x() - position.x() - 0.2f) < 1e-5f,
            "Repeated cancelling type/velocity changes use the final participation exactly once");
    body->setBodyType(STATIC)->setVelocity({100, 0});
    const auto stopped = object->getPosition();
    runtime.simulateFrame(dt);
    require(object->getPosition() == stopped, "Static bodies do not integrate a velocity request");
    body->setBodyType(DYNAMIC)->setVelocity({12, 0});
    runtime.simulateFrame(dt);
    require(std::abs(object->getPosition().x() - stopped.x() - 0.1f) < 1e-5f,
            "A live static-to-dynamic transition resumes movement");
    body->setIsSleeping(true);
    const auto sleeping = object->getPosition();
    runtime.simulateFrame(dt);
    require(object->getPosition() == sleeping, "Sleeping bodies leave active movement");
    body->setVelocity({12, 0});
    runtime.simulateFrame(dt);
    require(!body->getIsSleeping() && std::abs(object->getPosition().x() - sleeping.x() - 0.1f) < 1e-5f,
            "Velocity wake restores active movement");
    body->setVelocity({0, 0})->setGravity(false);
    for (int i = 0; i < 40; ++i)
    {
        runtime.simulateFrame(dt);
    }
    require(body->getIsSleeping(), "A quiet gravity-free unsupported body remains eligible to sleep");
    body->setGravity(true);
    runtime.simulateFrame(dt);
    require(!body->getIsSleeping() &&
                body->getVelocity().dot(
                    platformator_detail::RuntimeAccess::physicsManager()->getGravityVector()) > 0,
            "Gravity change wakes and integrates an unsupported body");
    auto *victim = runtime.createGameObject()->addComponent<Rigidbody>(DYNAMIC, false);
    const auto victimId = victim->getId();
    runtime.simulateFrameWithCustomCallback(dt, [&](double) {
        victim->getComponent<Rigidbody>()->setBodyType(KINEMATIC)->setVelocity({100, 0});
        victim->destroy();
    });
    require(runtime.getObjectById(victimId) == nullptr,
            "Retirement cancels pending participation before freeing");
    runtime.simulateFrame(dt);
}

void sameValuesAndWakeThresholds(size_t workers)
{
    tbb::global_control concurrency(tbb::global_control::max_allowed_parallelism, workers);
    platformator::Runtime runtime(physicsOnly());
    auto *object = runtime.createGameObject()->addComponent<Rigidbody>(DYNAMIC, false);
    auto *body = object->getComponent<Rigidbody>();
    runtime.simulateFrame(dt);

    body->setIsSleeping(true);
    body->setMass(body->getMass())->setFriction(body->getFriction())->setRestitution(body->getRestitution())
        ->setGravity(body->getGravity());
    require(body->getIsSleeping(), "Unchanged mass/material/gravity assignments keep a sleeping body asleep");
    body->setVelocity({0, 0})->setAngularVelocity(0)->setForce({0, 0})->setTorque(0);
    require(body->getIsSleeping(), "Zero motion and force assignments do not cross wake thresholds");

    body->setVelocity({0.0001f, 0})->setForce({0.0001f, 0});
    body->setAngularVelocity(0.00001f)->setTorque(0.00001f);
    require(body->getIsSleeping(), "Sub-threshold motion, force and torque assignments preserve sleep");
    body->setVelocity({1, 0});
    require(!body->getIsSleeping(), "Above-threshold velocity wakes a sleeping body");
    body->setIsSleeping(true)->setForce({1, 0});
    require(!body->getIsSleeping(), "Above-threshold force wakes a sleeping body");
    body->setIsSleeping(true)->setAngularVelocity(1);
    require(!body->getIsSleeping(), "Above-threshold angular velocity wakes a sleeping body");
    body->setIsSleeping(true)->setTorque(1);
    require(!body->getIsSleeping(), "Above-threshold torque wakes a sleeping body");

    auto *kinematic = runtime.createGameObject()->addComponent<Rigidbody>(KINEMATIC, false);
    auto *kinematicBody = kinematic->getComponent<Rigidbody>();
    kinematicBody->setVelocity({12, 0});
    runtime.simulateFrame(dt);
    const float before = kinematic->getX();
    kinematicBody->setVelocity({12, 0});
    runtime.simulateFrame(dt);
    require(std::abs(kinematic->getX() - before - 0.1f) < 1e-5f,
            "An unchanged kinematic velocity keeps the body in movement participation");
    kinematicBody->setVelocity({24, 0});
    runtime.simulateFrame(dt);
    require(std::abs(kinematic->getX() - before - 0.3f) < 1e-5f,
            "Changed kinematic velocity updates movement participation");
}

void sleepTransitionsAndGroups(size_t workers)
{
    tbb::global_control concurrency(tbb::global_control::max_allowed_parallelism, workers);
    platformator::Runtime runtime(physicsOnly());
    std::vector<Rigidbody *> bodies;
    for (int i = 0; i < 3; ++i)
    {
        auto *object = runtime.createGameObject()
                           ->setPosition({i * 0.995f, 0})
                           ->addComponent<BoxCollider>(1, 1)
                           ->addComponent<Rigidbody>(DYNAMIC, false);
        bodies.push_back(object->getComponent<Rigidbody>());
    }
    runtime.simulateFrame(dt);
    require(platformator_detail::RuntimeAccess::physicsManager()->getContactPairs().size() == 2,
            "Timer fixture establishes a real connected chain");

    bodies[0]->setSleepTimer(0.20);
    bodies[1]->setSleepTimer(0.05);
    bodies[2]->setSleepTimer(0.15);
    runtime.simulateFrame(dt);
    for (auto *body : bodies)
    {
        require(!body->getIsSleeping() && std::abs(body->getSleepTimer() - (0.05 + dt)) < 1e-12,
                "Connected candidates share their minimum prior quiet time");
    }

    auto *body = bodies.front();
    const double quietTime = body->getSleepTimer();
    body->wakeUp()->setIsSleeping(false);
    require(body->getSleepTimer() == quietTime, "No-op waking preserves accrued quiet time");
    body->setVelocity({10, 0})->setForce({3, 4})->setAngularVelocity(2)->setTorque(4);
    body->setIsSleeping(true);
    require(body->getVelocity().isZero() && body->getForce().isZero() && body->getAngularVelocity() == 0 &&
                body->getTorque() == 0 && body->getSleepTimer() == 0,
            "Sleep entry clears motion and accumulated force and resets time");
    body->setSleepTimer(0.03)->setIsSleeping(true);
    require(body->getSleepTimer() == 0.03, "Repeated sleep requests are also no-ops");
    body->wakeUp();
    require(body->getSleepTimer() == 0 && body->getVelocity().isZero(),
            "Actual waking resets time without motion");

    body->setAngularVelocity(1);
    runtime.simulateFrame(dt);
    for (auto *member : bodies)
    {
        require(!member->getIsSleeping() && member->getSleepTimer() == 0,
                "One moving connected member resets the whole group's quiet time");
    }
}

std::vector<std::array<uint32_t, 3>> solveAgainstSharedKinematicFloor(size_t workers)
{
    tbb::global_control concurrency(tbb::global_control::max_allowed_parallelism, workers);
    platformator::Runtime runtime(physicsOnly());
    constexpr int bodyCount = 384;
    auto *floor = runtime.createGameObject()
                      ->setPosition({float(bodyCount * 10), 9.5f})
                      ->addComponent<BoxCollider>(float(bodyCount * 20 + 20), 10)
                      ->addComponent<Rigidbody>(KINEMATIC, false);
    floor->getComponent<Rigidbody>()->setVelocity({12, 0});
    std::vector<Rigidbody *> bodies;
    for (int i = 0; i < bodyCount; ++i)
    {
        auto *object = runtime.createGameObject()
                           ->setPosition({float(i * 20), 0})
                           ->addComponent<BoxCollider>(10, 10)
                           ->addComponent<Rigidbody>(DYNAMIC, true);
        auto *body = object->getComponent<Rigidbody>();
        body->setVelocity({float(i % 5), 15})->setAngularVelocity(float(i % 3));
        bodies.push_back(body);
    }
    runtime.simulateFrame(dt);

    auto *physics = platformator_detail::RuntimeAccess::physicsManager();
    require(physics->getContactPairs().size() == bodyCount, "Each dynamic body contacts the shared kinematic floor");
    std::vector<std::array<uint32_t, 3>> state;
    for (const auto *body : bodies)
    {
        const auto &v = body->getVelocity();
        require(v.y() < 0, "The shared floor actually resolves each contact");
        state.push_back(std::bit_cast<std::array<uint32_t, 3>>(
            std::array{v.x(), v.y(), body->getAngularVelocity()}));
    }
    return state;
}

struct SolverResult
{
    std::vector<std::array<uint32_t, 3>> motion;
    std::vector<std::array<uint32_t, 7>> contacts;
    bool operator==(const SolverResult &) const = default;
};

SolverResult captureSolverResult(const PhysicsManager &physics, const std::vector<Rigidbody *> &bodies)
{
    SolverResult result;
    for (const auto *body : bodies)
    {
        const auto &v = body->getVelocity();
        result.motion.push_back(std::bit_cast<std::array<uint32_t, 3>>(
            std::array{v.x(), v.y(), body->getAngularVelocity()}));
    }
    for (const auto *pair : physics.getContactPairs())
    {
        const auto &points = pair->getCollision()->getContactPoints();
        require(points.count <= 2, "Each pair has at most two contact points");
        const auto a = pair->getObjectA()->getId(), b = pair->getObjectB()->getId();
        std::array<uint32_t, 7> contact{uint32_t(std::min(a, b)), uint32_t(std::max(a, b)), uint32_t(points.count)};
        for (size_t i = 0; i < points.count; ++i)
        {
            contact[3 + 2 * i] = std::bit_cast<uint32_t>(points.points[i].accumulatedNormalImpulse);
            contact[4 + 2 * i] = std::bit_cast<uint32_t>(points.points[i].accumulatedTangentImpulse);
        }
        result.contacts.push_back(contact);
    }
    std::sort(result.contacts.begin(), result.contacts.end());
    return result;
}

Rigidbody *addSolverBox(platformator::Runtime &runtime, float x, float y, float width, BodyType type)
{
    return runtime.createGameObject()
        ->setPosition({x, y})
        ->addComponent<BoxCollider>(width, 10.f)
        ->addComponent<Rigidbody>(type, false)
        ->getComponent<Rigidbody>();
}

SolverResult solveWithCancelledCaller(size_t workers, bool cancel)
{
    tbb::global_control concurrency(tbb::global_control::max_allowed_parallelism, workers);
    platformator::Runtime runtime(physicsOnly());
    constexpr int count = 128;
    for (float y : {-9.5f, 9.5f})
        addSolverBox(runtime, count * 10.f, y, count * 20.f + 20, STATIC);
    std::vector<Rigidbody *> bodies;
    for (int i = 0; i < count; ++i)
        bodies.push_back(addSolverBox(runtime, i * 20.f, 0, 10, DYNAMIC)->setVelocity({0, 15}));

    auto &physics = *platformator_detail::RuntimeAccess::physicsManager();
    physics.checkForCollisions();
    require(physics.getContactPairs().size() == count * 2, "Cancellation fixture starts with 256 cached contacts");
    require(physics.getGrid().getPendingNarrowPhasePairs()->empty(), "Narrow phase needs no worker batch");

    tbb::task_group caller;
    const auto status = caller.run_and_wait([&] {
        runtime.simulateFrameWithCustomCallback(dt, [&](double) {
            if (cancel)
                caller.cancel();
        });
    });
    require(status == (cancel ? tbb::canceled : tbb::complete), "The outer task reports its own cancellation");
    require(bodies.front()->getVelocity().y() < 0, "The simulation still applies contact impulses");
    return captureSolverResult(physics, bodies);
}

constexpr size_t cancellationTriggerPairs = 80;

void addCancellationTriggerPairs(platformator::Runtime &runtime)
{
    for (size_t i = 0; i < cancellationTriggerPairs; ++i)
    {
        const float x = float(i * 40);
        runtime.createGameObject()->setPosition({x, 0})->addComponent<CircleCollider>(2.f);
        runtime.createGameObject()
            ->setPosition({x + 2, 0})
            ->addComponent<CircleCollider>(2.f)
            ->getComponent<CircleCollider>()
            ->setIsTrigger(true);
    }
}

void simulateWithCancelledCaller(platformator::Runtime &runtime, const std::function<void()> &setup = {})
{
    tbb::task_group caller;
    const auto status = caller.run_and_wait([&] {
        auto *callerContext = tbb::task::current_context();
        runtime.simulateFrameWithCustomCallback(dt, [&](double) {
            require(tbb::task::current_context() == callerContext, "User hook keeps its caller's context");
            if (setup)
                setup();
            caller.cancel();
        });
    });
    require(status == tbb::canceled, "The parent still reports cancellation");
}

void cancellationPreservesMotionAndPendingContacts(size_t workers)
{
    tbb::global_control concurrency(tbb::global_control::max_allowed_parallelism, workers);
    platformator::Runtime runtime(physicsOnly());
    std::vector<Rigidbody *> bodies;
    for (int i = 0; i < 300; ++i)
        bodies.push_back(runtime.createGameObject()->addComponent<Rigidbody>(DYNAMIC, true)->getComponent<Rigidbody>());
    addCancellationTriggerPairs(runtime);

    const auto &physics = *platformator_detail::RuntimeAccess::physicsManager();
    const auto &grid = physics.getGrid();
    const auto queued = *grid.getPendingNarrowPhasePairs();
    require(queued.size() == cancellationTriggerPairs, "Trigger contacts are queued before the cancelled step");
    simulateWithCancelledCaller(runtime);

    require(std::ranges::all_of(bodies, [](const auto *body) {
                return body->getVelocity().y() > 0 && body->getGameObject()->getPosition().y() > 0;
            }), "The physics phase integrates forces and movement for all bodies");
    require(physics.getContactPairs().size() == cancellationTriggerPairs, "Narrow phase processes all pending pairs");
    require(grid.getPendingNarrowPhasePairs()->empty() && std::ranges::none_of(queued, [](const auto *pair) {
                return pair->getIsQueuedForNarrowPhase();
            }), "The consumed queue and its per-pair markers agree");
    runtime.simulateFrame(dt);
    require(physics.getContactPairs().size() == cancellationTriggerPairs, "The next step keeps all trigger contacts");
}

void cancellationPreservesDeferredGridFlush(size_t workers)
{
    tbb::global_control concurrency(tbb::global_control::max_allowed_parallelism, workers);
    platformator::Runtime runtime(physicsOnly());
    simulateWithCancelledCaller(runtime, [&] { addCancellationTriggerPairs(runtime); });
    const auto &physics = *platformator_detail::RuntimeAccess::physicsManager();
    require(physics.getGrid().getPendingNarrowPhasePairs()->size() == cancellationTriggerPairs,
            "The cancelled caller's cleanup still publishes all deferred collider pairs");
    runtime.simulateFrame(dt);
    require(physics.getContactPairs().size() == cancellationTriggerPairs,
            "All deferred trigger pairs reach narrow phase on the following step");
}

std::vector<SolverResult> solveConnectedColumns(size_t workers, bool sparse = false)
{
    tbb::global_control concurrency(tbb::global_control::max_allowed_parallelism, workers);
    platformator::Runtime runtime(physicsOnly());
    constexpr int columns = 144;
    addSolverBox(runtime, columns * 10.f, -9.5f, columns * 20.f + 20, STATIC);
    std::vector<Rigidbody *> bodies;
    for (int column = 0; column < columns; ++column)
        for (int row = 0; row < 4; ++row)
            bodies.push_back(addSolverBox(runtime, column * 20.f, row * 9.5f, 10, DYNAMIC)
                                 ->setVelocity({float(column % 5), float(10 + row)}));
    // The cap connects all columns after their independent constraints have entered the solver.
    bodies.push_back(addSolverBox(runtime, columns * 10.f, 38.f, columns * 20.f + 20, DYNAMIC));
    if (sparse)
        for (int i = 0; i < 3000; ++i)
            runtime.createGameObject()->addComponent<Rigidbody>(STATIC, false);

    auto &physics = *platformator_detail::RuntimeAccess::physicsManager();
    physics.checkForCollisions();
    require(physics.getContactPairs().size() == columns * 5, "The cap connects all four-body columns");
#if PLATFORMATOR_ENABLE_WORK_PROBES
    platformator_test::work.solverOrderedBatchDispatches = 0;
#endif
    std::vector<SolverResult> results;
    for (int step = 0; step < 2; ++step)
    {
        physics.applyPhysics(dt);
        // Cancel only around the solver to exercise the ordered-batch context in isolation.
        tbb::task_group caller;
        require(caller.run_and_wait([&] {
                    caller.cancel();
                    physics.resolveCollisions(dt);
                }) == tbb::canceled,
                "The enclosing task was cancelled");
        results.push_back(captureSolverResult(physics, bodies));
    }
#if PLATFORMATOR_ENABLE_WORK_PROBES
    if (workers > 1 && tbb::this_task_arena::max_concurrency() > 1)
        require(platformator_test::work.solverOrderedBatchDispatches == 2, "Both steps exercise ordered batches");
#endif
    return results;
}

std::vector<SolverResult> solveUnbalancedIslands(size_t workers)
{
    tbb::global_control concurrency(tbb::global_control::max_allowed_parallelism, workers);
    platformator::Runtime runtime(physicsOnly());
    auto addStaticBody = [&] { return runtime.createGameObject()->addComponent<Rigidbody>(STATIC, false); };
    auto *firstFiller = addStaticBody();
    addSolverBox(runtime, 0, 9.5f, 10, STATIC);
    std::vector<Rigidbody *> bodies{addSolverBox(runtime, 0, 0, 10, DYNAMIC)};
    for (int count : {128, 127})
    {
        const float origin = count == 128 ? 100.f : 4000.f;
        for (int i = 0; i < count; ++i)
            bodies.push_back(addSolverBox(runtime, origin + i * 20.f, 0, 10, DYNAMIC));
        bodies.push_back(addSolverBox(runtime, origin + count * 10.f, 9.5f, count * 20.f + 20, DYNAMIC));
    }

    auto &physics = *platformator_detail::RuntimeAccess::physicsManager();
    std::vector<SolverResult> results;
    const auto step = [&] {
        for (auto *body : bodies)
            body->setVelocity({0, body->getGameObject()->getY() == 0 ? 15.f : 0.f});
        physics.checkForCollisions();
        require(physics.getContactPairs().size() == 256, "Expected islands with 1, 128, and 127 constraints");
#if PLATFORMATOR_ENABLE_WORK_PROBES
        platformator_test::work.solverIslandJobs = 0;
        platformator_test::work.solverLargestIslandJob = 0;
#endif
        physics.resolveCollisions(dt);
#if PLATFORMATOR_ENABLE_WORK_PROBES
        if (workers > 1 && tbb::this_task_arena::max_concurrency() > 1)
            require(platformator_test::work.solverIslandJobs == 3 &&
                        platformator_test::work.solverLargestIslandJob == 128,
                    "The two expensive islands go into separate jobs");
#endif
        results.push_back(captureSolverResult(physics, bodies));
    };
    step();
    std::vector<GameObject *> fillers;
    for (int i = 0; i < 1200; ++i)
        fillers.push_back(addStaticBody());
    step();
    // Swap removal can reassign a previously initialized registered-body slot.
    auto *rail = bodies.back()->getGameObject();
    rail->removeComponent(RIGID_BODY);
    bodies.back() = rail->addComponent<Rigidbody>(DYNAMIC, false)->getComponent<Rigidbody>();
    firstFiller->destroy();
    step();
    for (auto *filler : fillers)
        filler->destroy();
    step();
    return results;
}
} // namespace

int main()
{
    headlessEnvironment();
    try
    {
        for (size_t workers : {size_t(1), size_t(6)})
        {
            baselineControls(workers);
            queueLifecycle(workers);
            contactMembershipRemoval(workers);
            bodyMembershipRemoval(workers);
            bodyParticipation(workers);
            sameValuesAndWakeThresholds(workers);
            sleepTransitionsAndGroups(workers);
            cancellationPreservesMotionAndPendingContacts(workers);
            cancellationPreservesDeferredGridFlush(workers);
        }
        const auto serial = solveAgainstSharedKinematicFloor(1);
        require(serial == solveAgainstSharedKinematicFloor(6),
                "Solving independent contacts in parallel preserves velocities exactly");
        require(solveWithCancelledCaller(1, false) == solveWithCancelledCaller(6, true),
                "Caller cancellation preserves solver velocities and cached impulses");
        const auto connectedSerial = solveConnectedColumns(1);
        require(connectedSerial == solveConnectedColumns(6) && connectedSerial == solveConnectedColumns(6, true),
                "Dense and sparse ordered batches preserve serial velocities and cached impulses");
        require(solveUnbalancedIslands(1) == solveUnbalancedIslands(6),
                "Dense and sparse job setup preserve serial outputs across registered slot changes");
        std::cout << "[PASS] physics bookkeeping contracts\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
