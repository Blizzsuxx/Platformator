#include "test_support.h"
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <platformator/boxcollider.h>
#include <platformator/circlecollider.h>
#include <platformator/collision.h>
#include <platformator/rigidbody.h>
#include <platformator/scriptcomponent.h>

using namespace test_support;
namespace
{
struct Contact : test_support::Behavior
{
    explicit Contact(int &count) : count(count)
    {
    }
    void onCollisionEnter(const Collision *collision, Collider *, double) override
    {
        ++count;
        const auto direction =
            (collision->getIncidentObject()->getWorldPosition() - collision->getReferenceObject()->getWorldPosition())
                .eval();
        require(collision->getNormal().allFinite() && collision->getNormal().dot(direction) >= -0.0001f,
                "Normals must orient by collider centers, including offsets");
        const auto &points = collision->getContactPoints();
        require(points.count > 0 && points.count <= 2, "Accepted geometry must have a bounded manifold");
        for (size_t i = 0; i < points.count; ++i)
            require(std::isfinite(points.points[i].separation), "Contact separation must be finite");
    }
    int &count;
};
void circleBox(Eigen::Vector2f center, bool expected, float rotation = 0,
               Eigen::Vector2f offset = Eigen::Vector2f::Zero())
{
    int contacts = 0;
    platformator::Runtime runtime(physicsOnly());
    auto *box = runtime.createGameObject()->setRotation(rotation)->addComponent<BoxCollider>(20, 20);
    auto *circle = runtime.createGameObject()
                       ->setPosition(center)
                       ->addComponent<CircleCollider>(1)
                       ->addComponent<ScriptComponent>();
    circle->getComponent<CircleCollider>()->setOffset(offset);
    circle->getComponent<ScriptComponent>()->addBehavior(new Contact(contacts));
    runtime.simulateFrame(dt);
    require((contacts == 1) == expected, "Circle/box closest-point oracle disagrees at " + std::to_string(center.x()) +
                                             "," + std::to_string(center.y()));
}
void circleCircle(Eigen::Vector2f offset, bool expected)
{
    int contacts = 0;
    platformator::Runtime runtime(physicsOnly());
    auto *a = runtime.createGameObject()->addComponent<CircleCollider>(2)->addComponent<ScriptComponent>();
    runtime.createGameObject()->setPosition(offset)->addComponent<CircleCollider>(2);
    a->getComponent<ScriptComponent>()->addBehavior(new Contact(contacts));
    runtime.simulateFrame(dt);
    require((contacts == 1) == expected,
            "Circle distance oracle must distinguish AABB candidates from confirmed contact");
}
void roundedBoxGap()
{
    // In double precision these OBBs have a 3e-5 separating gap. Float SAT
    // projections round to touching, but clipping must not publish an empty contact.
    int contacts = 0;
    platformator::Runtime runtime(physicsOnly());
    auto *a = runtime.createGameObject()
                  ->setPosition({-431.325165f, 138.851532f})
                  ->setRotation(-0.81292206f)
                  ->addComponent<BoxCollider>(28, 28)
                  ->addComponent<ScriptComponent>();
    runtime.createGameObject()
        ->setPosition({-450.082489f, 161.533493f})
        ->setRotation(-2.28055906f)
        ->addComponent<BoxCollider>(28, 28);
    a->getComponent<ScriptComponent>()->addBehavior(new Contact(contacts));
    runtime.simulateFrame(dt);
    require(contacts == 0, "A rounded SAT gap cannot publish an empty contact manifold");
}
void parameters()
{
    platformator::Runtime runtime(physicsOnly());
    auto *object = runtime.createGameObject()->addComponent<BoxCollider>(20, 20)->addComponent<Rigidbody>();
    auto *circle = runtime.createGameObject()->addComponent<CircleCollider>(5)->getComponent<CircleCollider>();
    const std::function<void()> invalid[] = {
        [&] { object->getComponent<BoxCollider>()->setWidth(0); },
        [&] { object->getComponent<BoxCollider>()->setHeight(-1); },
        [&] { object->getComponent<BoxCollider>()->setWidth(std::numeric_limits<float>::infinity()); },
        [&] { object->getComponent<Collider>()->setOffset({std::numeric_limits<float>::quiet_NaN(), 0}); },
        [&] { circle->setRadius(0); },
        [&] { object->setScale({1, 0}); },
        [&] { object->setScale({-1, 1}); },
        [&] { object->setPosition({std::numeric_limits<float>::infinity(), 0}); },
        [&] { object->setRotation(std::numeric_limits<float>::quiet_NaN()); },
        [&] { object->getComponent<Rigidbody>()->setMass(0); },
        [&] { object->getComponent<Rigidbody>()->setFriction(-1); },
        [&] { object->getComponent<Rigidbody>()->setRestitution(1.1f); },
        [&] { object->getComponent<Rigidbody>()->setVelocity({0, std::numeric_limits<float>::quiet_NaN()}); }};
    for (const auto &operation : invalid)
    {
        bool failed = false;
        try
        {
            operation();
        }
        catch (const std::invalid_argument &)
        {
            failed = true;
        }
        require(failed, "Invalid shape/body/transform inputs must reject at their boundary");
    }
    object->getComponent<Rigidbody>()->setBodyType(STATIC)->setMass(0);
    require(object->getComponent<Rigidbody>()->getInverseMass() == 0,
            "Static bodies have intentional zero inverse mass");
}
void unchangedProperties()
{
    platformator::Runtime runtime(physicsOnly());
    auto *object = runtime.createGameObject()->addComponent<BoxCollider>(20, 30)->addComponent<Rigidbody>();
    auto *box = object->getComponent<BoxCollider>();
    auto *body = object->getComponent<Rigidbody>();
    body->setGravity(false);
    runtime.simulateFrame(dt);
    body->setIsSleeping(true);
    runtime.simulateFrame(dt);
    require(!box->getIsQueuedForSync(), "Fixture must start with no queued geometry update");
    box->setWidth(20);
    box->setHeight(30);
    box->setOffset({0, 0});
    body->setMass(body->getMass())->setFriction(body->getFriction())->setRestitution(body->getRestitution())
        ->setGravity(body->getGravity());
    require(body->getIsSleeping(), "Unchanged properties must not wake a body");
    require(!box->getIsQueuedForSync(), "Unchanged collider properties must not queue geometry");
    const float originalInertia = body->getMomentOfInertia();
    box->setWidth(40);
    require(box->getIsQueuedForSync() && body->getMomentOfInertia() > originalInertia,
            "A changed box dimension queues geometry and refreshes body inertia");
    runtime.simulateFrame(dt);
    require(!box->getIsQueuedForSync(), "Changed box geometry settles at the next synchronization");
    box->setOffset({2, -3});
    require(box->getIsQueuedForSync(), "A changed finite offset queues collider geometry");
    runtime.simulateFrame(dt);
    body->setVelocity({10, 0});
    require(!body->getIsSleeping(), "Motion assignment still wakes a sleeping body");
    body->setBodyType(KINEMATIC)->setVelocity({3, 0})->setForce({2, 0});
    body->setBodyType(KINEMATIC);
    require(body->getVelocity() == Eigen::Vector2f(3, 0) && body->getForce().isZero(0),
            "Repeating a body type preserves its initialization semantics");
    auto *circleObject = runtime.createGameObject()->setPosition({1000, 0})->addComponent<CircleCollider>(5);
    auto *circle = circleObject->getComponent<CircleCollider>();
    runtime.simulateFrame(dt);
    require(!circle->getIsQueuedForSync(), "Circle fixture must start with no queued geometry update");
    circle->setRadius(5);
    circle->setOffset({0, 0});
    require(!circle->getIsQueuedForSync(), "Unchanged radius must not queue geometry");
    circle->setRadius(7);
    require(circle->getIsQueuedForSync(), "A changed circle radius queues geometry");
    runtime.simulateFrame(dt);
    circle->setOffset({1, 2});
    require(circle->getIsQueuedForSync(), "A changed finite circle offset queues geometry");
}
} // namespace
int main()
{
    headlessEnvironment();
    try
    {
        circleBox({10.95f, 10.4f}, false);
        circleBox({10.5f, 10.5f}, true);
        circleBox({0, 0}, true);
        circleBox({10.5f, 0}, true);
        circleBox({0, 14}, true, 0.7853981634f);
        circleBox({-100, 0}, true, 0, {110, 0});
        circleCircle({3, 3}, false);
        circleCircle({2, 2}, true);
        circleCircle({4, 0}, true);
        circleCircle({0, 0}, true);
        roundedBoxGap();
        parameters();
        unchangedProperties();
        std::cout << "[PASS] geometry and numeric boundaries\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
