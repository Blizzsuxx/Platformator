#include "test_support.h"
#include <iostream>
#include <oneapi/tbb/global_control.h>
#include <platformator/boxcollider.h>
#include <platformator/rigidbody.h>
#include <platformator/scriptcomponent.h>

using namespace test_support;
namespace
{
struct Events : test_support::Behavior
{
    int enter = 0, exit = 0;
    std::vector<int> trace;
    void onCollisionEnter(const Collision *, Collider *other, double) override
    {
        ++enter;
        trace.push_back(other->getId());
    }
    void onCollisionExit(Collider *other, double) override
    {
        require(other->getGameObject() != nullptr, "Exit other must remain readable");
        ++exit;
    }
};
struct Snapshot
{
    std::vector<int> trace;
    Eigen::Vector2f position, velocity;
    float rotation, angular;
};
Snapshot supports(size_t workers)
{
    tbb::global_control concurrency(tbb::global_control::max_allowed_parallelism, workers);
    platformator::Runtime runtime(physicsOnly());
    auto *object = runtime.createGameObject()
                       ->addComponent<BoxCollider>(2048, 20)
                       ->addComponent<Rigidbody>(DYNAMIC, false)
                       ->addComponent<ScriptComponent>();
    auto *body = object->getComponent<Rigidbody>();
    auto *events = new Events();
    object->getComponent<ScriptComponent>()->addBehavior(events);
    std::vector<GameObject *> floors;
    for (int i = 0; i < 128; ++i)
        floors.push_back(runtime.createGameObject()
                             ->setPosition({-1016.f + i * 16.f, 19})
                             ->addComponent<BoxCollider>(15, 20)
                             ->addComponent<Rigidbody>(STATIC, false));
    const auto freeze = [&](double) {
        object->setPosition({0, 0})->setRotation(0);
        body->setVelocity({0, 0})->setAngularVelocity(0);
    };
    runtime.simulateFrameWithCustomCallback(dt, freeze);
    require(body->getSupportContactCount() == 128, "Frozen 128-support layout must count every contribution exactly");
    const Snapshot snapshot{events->trace, object->getPosition(), body->getVelocity(), object->getRotation(),
                            body->getAngularVelocity()};
    for (int i = 0; i < 4; ++i)
    {
        runtime.simulateFrameWithCustomCallback(dt, freeze);
        require(body->getSupportContactCount() == 128, "Repeated manifold updates cannot drift support counts");
    }
    auto *floor = floors[0]->getComponent<BoxCollider>();
    floor->setCollisionMask(0);
    runtime.simulateFrameWithCustomCallback(dt, freeze);
    require(body->getSupportContactCount() == 127, "Filter removal must balance exactly one support");
    floor->setCollisionMask(1);
    runtime.simulateFrameWithCustomCallback(dt, freeze);
    require(body->getSupportContactCount() == 128, "Unchanged geometry must regain re-enabled support");
    floor->setIsTrigger(true);
    runtime.simulateFrameWithCustomCallback(dt, freeze);
    require(body->getSupportContactCount() == 127, "Triggers retain overlap without contributing support");
    floor->setIsTrigger(false);
    runtime.simulateFrameWithCustomCallback(dt, freeze);
    require(body->getSupportContactCount() == 128, "Trigger response change must restore support");
    floors[0]->setActive(false)->setActive(true);
    runtime.simulateFrameWithCustomCallback(dt, freeze);
    require(body->getSupportContactCount() == 128, "Off/on coalescing cannot lose collider membership");
    floors[0]->removeComponent(RIGID_BODY);
    runtime.simulateFrameWithCustomCallback(dt, freeze);
    require(body->getSupportContactCount() == 127, "Removing a body must remove its old support relationship");
    return snapshot;
}
void referenceSwaps()
{
    platformator::Runtime runtime(physicsOnly());
    auto *a = runtime.createGameObject()
                  ->addComponent<BoxCollider>(40, 20)
                  ->addComponent<Rigidbody>(DYNAMIC, false)
                  ->addComponent<ScriptComponent>();
    auto *b =
        runtime.createGameObject()->setPosition({0, 15})->addComponent<BoxCollider>(40, 20)->addComponent<Rigidbody>(
            STATIC, false);
    for (int i = 0; i < 20; ++i)
    {
        runtime.simulateFrameWithCustomCallback(dt, [&](double) {
            a->setPosition({0, 0})->setRotation(i % 2 ? 0 : 0.4f);
            b->setRotation(i % 2 ? 0.4f : 0);
            a->getComponent<Rigidbody>()->setVelocity({0, 0})->setAngularVelocity(0);
        });
        require(a->getComponent<Rigidbody>()->getSupportContactCount() == 1,
                "Reference/incident changes cannot drop or duplicate canonical support contributions");
    }
}
void stationary()
{
    platformator::Runtime runtime(physicsOnly());
    auto *a = runtime.createGameObject()->addComponent<BoxCollider>(20, 20)->addComponent<ScriptComponent>();
    auto *events = new Events();
    a->getComponent<ScriptComponent>()->addBehavior(events);
    auto *b = runtime.createGameObject()->addComponent<BoxCollider>(20, 20);
    for (int i = 0; i < 3; ++i)
        runtime.simulateFrame(dt);
    require(events->enter == 1 && events->exit == 0,
            "Stationary observed contacts remain silent without geometry work");
    b->getComponent<BoxCollider>()->setCollisionMask(0);
    runtime.simulateFrame(dt);
    require(events->exit == 1, "Mask removal must exit once");
    b->getComponent<BoxCollider>()->setCollisionMask(1);
    runtime.simulateFrame(dt);
    require(events->enter == 2, "Re-enabled unchanged overlap must enter again");
    runtime.simulateFrameWithCustomCallback(dt, [&](double) {
        b->setActive(false)->setActive(true);
        b->getComponent<BoxCollider>()->setCollisionMask(0);
        b->getComponent<BoxCollider>()->setCollisionMask(1);
    });
    require(events->enter == 2 && events->exit == 1, "Net-unchanged toggles cannot manufacture exit/enter");
    b->destroy();
    require(events->exit == 2, "Eligible survivor receives the retirement exit synchronously while idle");
}
void scriptsPreserveSleepingSupport()
{
    platformator::Runtime runtime(physicsOnly());
    auto *object =
        runtime.createGameObject()->addComponent<BoxCollider>(20, 20)->addComponent<Rigidbody>(DYNAMIC, false);
    auto *floor =
        runtime.createGameObject()->setPosition({0, 19})->addComponent<BoxCollider>(40, 20)->addComponent<Rigidbody>(
            STATIC, false);
    runtime.simulateFrame(dt);
    auto *body = object->getComponent<Rigidbody>();
    require(body->getSupportContactCount() == 1, "Control contact contributes one physical support");
    body->setIsSleeping(true);
    object->addComponent<ScriptComponent>()->getComponent<ScriptComponent>()->addBehavior(new Events);
    runtime.simulateFrame(dt);
    object->removeComponent(SCRIPT);
    object->addComponent<ScriptComponent>()->getComponent<ScriptComponent>()->addBehavior(new Events);
    runtime.simulateFrame(dt);
    require(body->getIsSleeping() && body->getSupportContactCount() == 1,
            "Script replacement preserves sleeping physical support");
    object->removeComponent(RIGID_BODY);
    object->addComponent<Rigidbody>(DYNAMIC, false);
    runtime.simulateFrame(dt);
    body = object->getComponent<Rigidbody>();
    require(body->getSupportContactCount() == 1, "Body replacement transfers support without geometry movement");
    body->setIsSleeping(true);
    floor->removeComponent(COLLIDER);
    require(!body->getIsSleeping() && body->getSupportContactCount() == 0,
            "Losing final support wakes the replacement body");
}
} // namespace
int main()
{
    headlessEnvironment();
    try
    {
        const auto reference = supports(1);
        for (int i = 0; i < 5; ++i)
        {
            const auto parallel = supports(6);
            require(parallel.trace == reference.trace, "Canonical event order must not depend on worker completion");
            require((parallel.position - reference.position).norm() <= 1e-5f &&
                        (parallel.velocity - reference.velocity).norm() <= 1e-5f &&
                        std::abs(parallel.rotation - reference.rotation) <= 1e-5f &&
                        std::abs(parallel.angular - reference.angular) <= 1e-5f,
                    "Frozen serial/parallel physical snapshots must agree within 1e-5");
        }
        stationary();
        scriptsPreserveSleepingSupport();
        referenceSwaps();
        std::cout << "[PASS] deterministic supports and persistent contacts\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
