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
    const std::function<void()> invalid[] = {
        [&] { object->getComponent<BoxCollider>()->setWidth(0); },
        [&] { object->getComponent<BoxCollider>()->setHeight(-1); },
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
        roundedBoxGap();
        parameters();
        std::cout << "[PASS] geometry and numeric boundaries\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
