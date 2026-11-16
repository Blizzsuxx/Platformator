#include "gamemanager.h"
#include "physicsmanager.h"
#include "runtimeaccess.h"
#include "test_support.h"
#include <iostream>
#include <oneapi/tbb/global_control.h>
#include <platformator/boxcollider.h>
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
        }
        std::cout << "[PASS] physics bookkeeping contracts\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
