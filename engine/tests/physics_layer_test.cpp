#include "test_support.h"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <oneapi/tbb/global_control.h>
#include <platformator/boxcollider.h>
#include <platformator/gameobject.h>
#include <platformator/rigidbody.h>
#include <platformator/scriptcomponent.h>

using namespace test_support;

namespace
{
class ContactCounterBehavior : public test_support::Behavior
{
  public:
    std::atomic<int> enters{0};
    std::atomic<int> exits{0};

    void onCollisionEnter(const Collision *collision, Collider *other, double) override
    {
        (void)collision;
        (void)other;
        enters.fetch_add(1, std::memory_order_relaxed);
    }

    void onCollisionExit(Collider *other, double) override
    {
        (void)other;
        exits.fetch_add(1, std::memory_order_relaxed);
    }
};

ContactCounterBehavior *attach_counter(GameObject *obj)
{
    if (!obj->getComponent<ScriptComponent>())
        obj->addComponent<ScriptComponent>();
    auto *counter = new ContactCounterBehavior();
    obj->getComponent<ScriptComponent>()->addBehavior(counter);
    return counter;
}

void test_collider_filter_defaults()
{
    platformator::Runtime runtime(physicsOnly());
    auto *obj = runtime.createGameObject();
    obj->addComponent<BoxCollider>(1.0f, 1.0f);
    auto *collider = obj->getComponent<BoxCollider>();
    require(collider->getCollisionGroup() == 1ULL && collider->getCollisionMask() == 1ULL,
            "Collider group and mask must default to bit 0");
}

void test_collider_mask_solid_filtering(size_t workers)
{
    tbb::global_control concurrency(tbb::global_control::max_allowed_parallelism, workers);
    platformator::Runtime runtime(physicsOnly());

    constexpr uint64_t kLayer1Bit = uint64_t{1} << 1;
    constexpr uint64_t kLayer2Bit = uint64_t{1} << 2;

    auto *a = runtime.createGameObject();
    a->setPosition(Eigen::Vector2f(0.0f, 0.0f));
    a->addComponent<Rigidbody>(DYNAMIC, false);
    a->addComponent<BoxCollider>(1.0f, 1.0f);
    auto *colA = a->getComponent<BoxCollider>();
    colA->setCollisionGroup(kLayer1Bit);
    colA->setCollisionMask(0xFFFFFFFFULL & ~kLayer2Bit);
    auto *aEvents = attach_counter(a);

    auto *b = runtime.createGameObject();
    b->setPosition(Eigen::Vector2f(0.25f, 0.0f));
    b->addComponent<Rigidbody>(STATIC, false);
    b->addComponent<BoxCollider>(1.0f, 1.0f);
    auto *colB = b->getComponent<BoxCollider>();
    colB->setCollisionGroup(kLayer2Bit);
    colB->setCollisionMask(0xFFFFFFFFULL & ~kLayer1Bit);
    auto *bEvents = attach_counter(b);

    for (int step = 0; step < 5; ++step)
        runtime.simulateFrame(dt);

    require(aEvents->enters.load() == 0 && bEvents->enters.load() == 0,
            "Disabled collider layer mask pair must not produce solid collision enter events");

    // Dynamically move B to layer 1 -> broadphase must allow contact to begin.
    colB->setCollisionGroup(kLayer1Bit);
    colB->setCollisionMask(0xFFFFFFFFULL & ~kLayer2Bit);
    for (int step = 0; step < 3; ++step)
        runtime.simulateFrame(dt);

    require(aEvents->enters.load() >= 1 && bEvents->enters.load() >= 1,
            "Dynamic setCollisionGroup to matching mask must allow contact to begin");

    // Dynamically move B back to layer 2 -> collision must exit cleanly.
    colB->setCollisionGroup(kLayer2Bit);
    colB->setCollisionMask(0xFFFFFFFFULL & ~kLayer1Bit);
    for (int step = 0; step < 3; ++step)
        runtime.simulateFrame(dt);

    require(aEvents->exits.load() >= 1 && bEvents->exits.load() >= 1,
            "Dynamic setCollisionGroup to masked-out bit must emit collision exit");
}

void test_trigger_filtering()
{
    platformator::Runtime runtime(physicsOnly());

    constexpr uint64_t kLayer3Bit = uint64_t{1} << 3;
    constexpr uint64_t kLayer4Bit = uint64_t{1} << 4;

    auto *sensor = runtime.createGameObject();
    sensor->setPosition(Eigen::Vector2f(0.0f, 0.0f));
    sensor->addComponent<Rigidbody>(DYNAMIC, false);
    sensor->addComponent<BoxCollider>(1.0f, 1.0f);
    auto *sensorCollider = sensor->getComponent<BoxCollider>();
    sensorCollider->setIsTrigger(true);
    sensorCollider->setCollisionGroup(kLayer3Bit);
    sensorCollider->setCollisionMask(0xFFFFFFFFULL & ~kLayer4Bit);
    auto *sensorEvents = attach_counter(sensor);

    auto *target = runtime.createGameObject();
    target->setPosition(Eigen::Vector2f(0.1f, 0.0f));
    target->addComponent<Rigidbody>(STATIC, false);
    target->addComponent<BoxCollider>(1.0f, 1.0f);
    auto *targetCollider = target->getComponent<BoxCollider>();
    targetCollider->setCollisionGroup(kLayer4Bit);
    targetCollider->setCollisionMask(0xFFFFFFFFULL & ~kLayer3Bit);

    for (int step = 0; step < 4; ++step)
        runtime.simulateFrame(dt);

    require(sensorEvents->enters.load() == 0,
            "Disabled collider mask pair must filter trigger collisions as well");
}
} // namespace

int main()
{
    test_collider_filter_defaults();
    test_collider_mask_solid_filtering(1);
    test_collider_mask_solid_filtering(4);
    test_trigger_filtering();
    std::cout << "physics_layer_test passed\n";
    return 0;
}
