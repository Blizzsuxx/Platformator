#include "test_support.h"
#include <cmath>
#include <iostream>
#include <platformator/boxcollider.h>
#include <platformator/rigidbody.h>
#include <platformator/scriptcomponent.h>

using namespace test_support;
namespace
{
struct Contacts : test_support::Behavior
{
    explicit Contacts(int &enters) : enters(enters)
    {
    }
    void onCollisionEnter(const Collision *, Collider *, double) override
    {
        ++enters;
    }
    int &enters;
};
void parentMotion(bool childFirst)
{
    int enters = 0;
    platformator::Runtime runtime(physicsOnly());
    auto *first = runtime.createGameObject();
    auto *second = runtime.createGameObject();
    auto *parent = childFirst ? second : first;
    auto *child = childFirst ? first : second;
    child->addComponent<BoxCollider>(10, 10)->addComponent<ScriptComponent>();
    child->getComponent<ScriptComponent>()->addBehavior(new Contacts(enters));
    parent->addChild(child);
    runtime.createGameObject()->setPosition({1000, 0})->addComponent<BoxCollider>(10, 10);
    runtime.simulateFrame(dt);
    parent->setPosition({1000, 0});
    require(child->getPosition() == Eigen::Vector2f(1000, 0), "Main-thread reads must see parent edits immediately");
    runtime.simulateFrame(dt);
    require(enters == 1, "Parent-only movement must automatically refresh a collider-only child");
    auto *newParent = runtime.createGameObject()->setPosition({500, 0});
    newParent->addChild(child);
    require(child->getPosition() == Eigen::Vector2f(1000, 0), "Dirty reparent preserves world translation");
    newParent->setPosition({700, 0});
    require(child->getPosition() == Eigen::Vector2f(1200, 0), "Reparented reads use the new ancestor");
    runtime.simulateFrame(dt);
    require(child->getComponent<BoxCollider>()->getGridCellRange().minX > 0,
            "Reparented geometry must publish automatically");
}
void integratedHierarchy()
{
    platformator::Runtime runtime(physicsOnly());
    auto *parent = runtime.createGameObject()->setPosition({10, 0})->addComponent<Rigidbody>(KINEMATIC, false);
    auto *child = runtime.createGameObject()
                      ->setPosition({30, 0})
                      ->addComponent<Rigidbody>(KINEMATIC, false)
                      ->addComponent<BoxCollider>(5, 5);
    parent->addChild(child);
    parent->getComponent<Rigidbody>()->setVelocity({12, 0});
    child->getComponent<Rigidbody>()->setVelocity({24, 0});
    auto *leaf = runtime.createGameObject()->setPosition({35, 0})->addComponent<BoxCollider>(2, 2);
    child->addChild(leaf);
    for (int i = 0; i < 120; ++i)
        runtime.simulateFrame(dt);
    require(std::abs(child->getX() - 66) < 0.002f && std::abs(leaf->getX() - 71) < 0.002f,
            "Parent/child workers integrate their own local motion without overwriting ancestor translation");
}
} // namespace
int main()
{
    headlessEnvironment();
    try
    {
        parentMotion(false);
        parentMotion(true);
        integratedHierarchy();
        std::cout << "[PASS] hierarchy transforms\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
