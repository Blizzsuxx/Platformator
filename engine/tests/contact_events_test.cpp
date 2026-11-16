#include "test_support.h"
#include "gamemanager.h"
#include "physicsmanager.h"
#include "runtimeaccess.h"
#include "runtime_work_probe.h"
#include <platformator/boxcollider.h>
#include <platformator/circlecollider.h>
#include <platformator/scriptcomponent.h>
#include <oneapi/tbb/global_control.h>
#include <functional>
#include <iostream>
#include <vector>

using namespace test_support;
template <typename T>
concept HasStayCallback =
    requires(T &value, const Collision *contact, Collider *other) { value.onCollisionStay(contact, other, 0.0); };
template <typename T>
concept HasStayEvent = requires { T::COLLISION_STAY; };
static_assert(!HasStayCallback<::Behavior>);
static_assert(!HasStayEvent<PhysicsEvent>);
namespace
{
struct Counts
{
    int enters = 0, exits = 0;
};
struct Listener : test_support::Behavior
{
    explicit Listener(Counts &counts) : counts(counts)
    {
    }
    void onCollisionEnter(const Collision *collision, Collider *other, double) override
    {
        require(collision && other && other->getGameObject(), "Enter payload must be live");
        ++counts.enters;
        if (enterAction)
            enterAction();
    }
    void onCollisionExit(Collider *other, double) override
    {
        require(other && other->getGameObject(), "Exit endpoint must remain readable through dispatch");
        ++counts.exits;
        if (exitAction)
            exitAction();
    }
    Counts &counts;
    std::function<void()> enterAction, exitAction;
};
Listener *attach(GameObject *object, Counts &counts)
{
    if (!object->getComponent<ScriptComponent>())
        object->addComponent<ScriptComponent>();
    auto *listener = new Listener(counts);
    object->getComponent<ScriptComponent>()->addBehavior(listener);
    return listener;
}
std::vector<std::string> collect(PhysicsManager *physics)
{
    std::vector<std::string> result;
    for (const auto &event : physics->getPendingPhysicsEvents())
    {
        require(event.colliderA->getHandle() < event.colliderB->getHandle(), "Canonical endpoints include generations");
        if (event.type == PhysicsEvent::COLLISION_ENTER)
            require(event.collision != nullptr, "Confirmed Enter has contact payload");
        if (event.type == PhysicsEvent::COLLISION_EXIT)
            require(event.collision == nullptr, "Exit does not retain contact memory");
        result.push_back(std::to_string(int(event.type)) + ":" + event.colliderA->getGameObject()->getName() + "/" +
                         event.colliderB->getGameObject()->getName());
    }
    return result;
}
std::vector<std::string> transitions(size_t workers, int recipients)
{
    tbb::global_control limit(tbb::global_control::max_allowed_parallelism, workers);
    Counts aCounts, bCounts;
    platformator::Runtime runtime(physicsOnly());
    std::vector<GameObject *> targets;
    for (int i = 0; i < 128; ++i)
    {
        auto *a = runtime.createGameObject()
                      ->setName("a" + std::to_string(i))
                      ->setPosition({float(i) * 20, 0})
                      ->addComponent<CircleCollider>(2);
        auto *b = runtime.createGameObject()
                      ->setName("b" + std::to_string(i))
                      ->setPosition({float(i) * 20 + 3, 3})
                      ->addComponent<CircleCollider>(2);
        a->getComponent<CircleCollider>()->setIsTrigger(true);
        b->getComponent<CircleCollider>()->setIsTrigger(true);
        if (recipients > 0)
            attach(a, aCounts);
        if (recipients > 1)
            attach(b, bCounts);
        targets.push_back(b);
    }
    runtime.simulateFrame(dt); // Startup and separated-shape candidate publication.
    require(aCounts.enters == 0 && bCounts.enters == 0, "AABB overlap is not confirmed circle contact");
    auto *physics = platformator_detail::RuntimeAccess::physicsManager();
    for (int i = 0; i < 128; ++i)
        targets[i]->setPosition({float(i) * 20 + 2, 2});
    platformator_detail::RuntimeAccess::gameManager().commitMutations();
    physics->checkForCollisions();
    auto enters = collect(physics);
    require(enters.size() == 128, "Every absent-to-present contact generates Enter, even without scripts");
    for (const auto &event : physics->getPendingPhysicsEvents())
        require(event.type == PhysicsEvent::COLLISION_ENTER, "Only entry edges");
    physics->handlePendingPhysicsEvents(dt);
    require(aCounts.enters == (recipients > 0 ? 128 : 0) && bCounts.enters == (recipients > 1 ? 128 : 0),
            "Pair generation differs from endpoint delivery");
    // Movement leaves AABB candidates overlapping but separates circle geometry.
    for (int i = 0; i < 128; ++i)
        targets[i]->setPosition({float(i) * 20 + 3, 3});
    platformator_detail::RuntimeAccess::gameManager().commitMutations();
    physics->checkForCollisions();
    auto exits = collect(physics);
    require(exits.size() == 128, "Every geometric ending generates exactly one Exit");
    for (const auto &event : physics->getPendingPhysicsEvents())
        require(event.type == PhysicsEvent::COLLISION_EXIT, "Only exit edges");
    physics->handlePendingPhysicsEvents(dt);
    require(aCounts.exits == (recipients > 0 ? 128 : 0) && bCounts.exits == (recipients > 1 ? 128 : 0),
            "Each eligible endpoint receives Exit once");
    enters.insert(enters.end(), exits.begin(), exits.end());
    return enters;
}
void mixedTransitionOrder()
{
    Counts counts;
    std::vector<std::string> trace;
    platformator::Runtime runtime(physicsOnly());
    // Give the entering pair lower handles so pair-only sorting would put Enter first.
    auto *newA = runtime.createGameObject()->setPosition({50, 0})->addComponent<CircleCollider>(2);
    auto *newB = runtime.createGameObject()->setPosition({53, 3})->addComponent<CircleCollider>(2);
    auto *oldA = runtime.createGameObject()->addComponent<CircleCollider>(2);
    auto *oldB = runtime.createGameObject()->setPosition({2, 2})->addComponent<CircleCollider>(2);
    auto *newListenerA = attach(newA, counts);
    auto *newListenerB = attach(newB, counts);
    auto *oldListenerA = attach(oldA, counts);
    auto *oldListenerB = attach(oldB, counts);
    runtime.simulateFrame(dt);
    newListenerA->enterAction = [&] { trace.push_back("enter:newA"); };
    newListenerB->enterAction = [&] { trace.push_back("enter:newB"); };
    oldListenerA->exitAction = [&] { trace.push_back("exit:oldA"); };
    oldListenerB->exitAction = [&] { trace.push_back("exit:oldB"); };
    newB->setPosition({52, 2});
    oldB->setPosition({3, 3});
    platformator_detail::RuntimeAccess::gameManager().commitMutations();
    auto *physics = platformator_detail::RuntimeAccess::physicsManager();
    physics->checkForCollisions();
    require(physics->getPendingPhysicsEvents().size() == 2, "Mixed batch contains one ending and one beginning");
    physics->handlePendingPhysicsEvents(dt);
    require(trace == std::vector<std::string>{"exit:oldA", "exit:oldB", "enter:newA", "enter:newB"},
            "Exit family precedes Enter, with canonical endpoint delivery inside each pair");
    std::cout << "mixed batch:";
    for (const auto &entry : trace)
        std::cout << ' ' << entry;
    std::cout << '\n';
}
void quiet()
{
    Counts counts;
    platformator::Runtime runtime(physicsOnly());
    auto *a = runtime.createGameObject()->addComponent<BoxCollider>(20, 20);
    auto *b = runtime.createGameObject()->addComponent<BoxCollider>(20, 20);
    attach(a, counts);
    runtime.simulateFrame(dt);
    auto *physics = platformator_detail::RuntimeAccess::physicsManager();
    for (int i = 0; i < 100; ++i)
    {
        physics->checkForCollisions();
        require(physics->getPendingPhysicsEvents().empty(), "Persistent contacts generate no recurring event");
        physics->handlePendingPhysicsEvents(dt);
    }
    require(counts.enters == 1 && counts.exits == 0, "Quiet contacts are silent after Enter");
    b->setPosition({1000, 0});
    runtime.simulateFrame(dt);
    require(counts.exits == 1, "Separation exits once");
    b->setPosition({0, 0});
    runtime.simulateFrame(dt);
    require(counts.enters == 2, "Re-contact produces a real new Enter");
}
void lateScript()
{
    Counts early, late, replacement, additional;
    platformator::Runtime runtime(physicsOnly());
    auto *a = runtime.createGameObject()->addComponent<BoxCollider>(20, 20);
    auto *b = runtime.createGameObject()->addComponent<BoxCollider>(20, 20);
    runtime.simulateFrame(dt);
    attach(a, late);
    attach(b, early);
    runtime.simulateFrame(dt);
    require(late.enters == 0 && early.enters == 0, "Late startup does not replay Enter");
    attach(a, additional);
    runtime.simulateFrame(dt);
    require(additional.enters == 0, "Adding a behavior to a live script does not replay Enter");
    a->removeComponent(SCRIPT);
    attach(a, replacement);
    runtime.simulateFrame(dt);
    require(replacement.enters == 0, "Replacing a script does not change contacts");
    b->getComponent<BoxCollider>()->setCollisionMask(0);
    runtime.simulateFrame(dt);
    require(replacement.exits == 1 && early.exits == 1, "Late recipients receive the actual Exit without Enter");
}
void retirementDuringEnter()
{
    Counts first, second;
    platformator::Runtime runtime(physicsOnly());
    auto *a = runtime.createGameObject()->addComponent<BoxCollider>(20, 20);
    auto *b = runtime.createGameObject()->addComponent<BoxCollider>(20, 20);
    auto *listener = attach(a, first);
    attach(b, second);
    listener->enterAction = [&] { b->destroy(); };
    runtime.simulateFrame(dt);
    require(first.enters == 1 && second.enters == 0 && first.exits == 1,
            "Retirement suppresses later Enter while cleanup Exit retains readable endpoints");
}
void eligibilityAtDelivery()
{
    Counts first, second;
    platformator::Runtime runtime(physicsOnly());
    auto *a = runtime.createGameObject()->addComponent<BoxCollider>(20, 20);
    auto *b = runtime.createGameObject()->addComponent<BoxCollider>(20, 20);
    auto *listener = attach(a, first);
    attach(b, second);
    listener->enterAction = [&] { b->removeComponent(SCRIPT); };
    runtime.simulateFrame(dt);
    require(first.enters == 1 && second.enters == 0, "Eligibility is checked independently at each endpoint delivery");
    attach(b, second);
    runtime.simulateFrame(dt);
    require(second.enters == 0, "New recipient does not replay the skipped event");
    b->setPosition({1000, 0});
    runtime.simulateFrame(dt);
    require(first.exits == 1 && second.exits == 1, "Replacement receives the actual future ending");
}
void cleanupFailure()
{
    Counts counts;
    platformator::Runtime runtime(physicsOnly());
    auto *a = runtime.createGameObject()->addComponent<BoxCollider>(20, 20);
    auto *b = runtime.createGameObject()->addComponent<BoxCollider>(20, 20);
    const auto id = b->getId();
    auto *listener = attach(a, counts);
    runtime.simulateFrame(dt);
    listener->exitAction = [] { throw std::runtime_error("intentional cleanup error"); };
    bool failed = false;
    try
    {
        runtime.simulateFrameWithCustomCallback(dt, [&](double) { b->destroy(); });
    }
    catch (const std::runtime_error &)
    {
        failed = true;
    }
    require(failed && counts.exits == 1 && runtime.getObjectById(id) == nullptr,
            "Cleanup errors cannot prevent retirement or duplicate the ending");
    require(platformator_detail::RuntimeAccess::physicsManager()->getPendingPhysicsEvents().empty(),
            "Cleanup failure drains its batch");
    runtime.simulateFrame(dt);
}
void mutationAndFailure()
{
    Counts first, second;
    platformator::Runtime runtime(physicsOnly());
    auto *a = runtime.createGameObject()->addComponent<BoxCollider>(20, 20);
    auto *b = runtime.createGameObject()->addComponent<BoxCollider>(20, 20);
    auto *listener = attach(a, first);
    attach(b, second);
    listener->enterAction = [&] {
        a->removeComponent(SCRIPT);
        throw std::runtime_error("intentional endpoint error");
    };
    bool failed = false;
    try
    {
        runtime.simulateFrame(dt);
    }
    catch (const std::runtime_error &)
    {
        failed = true;
    }
    require(failed && first.enters == 1 && second.enters == 1, "Endpoint failure must not suppress the other delivery");
    require(platformator_detail::RuntimeAccess::physicsManager()->getPendingPhysicsEvents().empty(),
            "Failed batch is drained");
    b->setPosition({1000, 0});
    runtime.simulateFrame(dt);
    require(first.exits == 0 && second.exits == 1, "Retired script is skipped while survivor receives Exit");
}
} // namespace
int main(int argc, char **argv)
{
    headlessEnvironment();
    try
    {
        const auto reference = transitions(1, 0);
        for (auto workers : {size_t(1), size_t(6)})
            for (int recipients = 0; recipients <= 2; ++recipients)
                require(transitions(workers, recipients) == reference,
                        "Generated histories depend on neither scripts nor workers");
        if (argc < 2 || std::string(argv[1]) != "--transitions-only")
        {
            mixedTransitionOrder();
            quiet();
            lateScript();
            retirementDuringEnter();
            eligibilityAtDelivery();
            cleanupFailure();
            mutationAndFailure();
        }
        std::cout << "[PASS] contact transitions, recipients, silence, script lifecycle\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
