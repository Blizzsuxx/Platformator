#include "test_support.h"
#include <cmath>
#include <iostream>
#include <oneapi/tbb/global_control.h>
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
void integratedHierarchy(size_t workers)
{
    tbb::global_control concurrency(tbb::global_control::max_allowed_parallelism, workers);
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
    runtime.simulateFrame(dt);
    require(std::abs(child->getX() - 30.3f) < 0.002f && std::abs(leaf->getX() - 35.3f) < 0.002f,
            "One synchronized batch publishes parent and child motion together");
    for (int i = 0; i < 119; ++i)
        runtime.simulateFrame(dt);
    require(std::abs(child->getX() - 66) < 0.002f && std::abs(leaf->getX() - 71) < 0.002f,
            "Parent/child workers integrate their own local motion without overwriting ancestor translation");
}
void immediateHierarchy()
{
    platformator::Runtime runtime(physicsOnly());
    auto *root = runtime.createGameObject()->setPosition({10, 4});
    auto *middle = runtime.createGameObject()->setPosition({20, 7});
    auto *leaf = runtime.createGameObject()->setPosition({25, 9});
    root->addChild(middle);
    middle->addChild(leaf);
    root->setPosition({30, 4});
    require(leaf->getPosition() == Eigen::Vector2f(45, 9), "Nested reads reflect unsynchronized ancestor edits");
    for (int read = 0; read < 64; ++read)
        require(leaf->getPosition() == Eigen::Vector2f(45, 9), "Repeated reads preserve the current nested position");
    middle->setPosition({40, 8});
    require(leaf->getPosition() == Eigen::Vector2f(45, 10), "Nested reads reflect unsynchronized local edits");
    require(root->removeChild(middle), "Detach succeeds");
    root->setPosition({50, 4});
    require(leaf->getPosition() == Eigen::Vector2f(45, 10), "Detached descendants retain their positions");
    root->addChild(middle);
    root->setPosition({55, 4});
    require(leaf->getPosition() == Eigen::Vector2f(50, 10), "Reparented descendants follow their new ancestor");
}
} // namespace
int main()
{
    headlessEnvironment();
    try
    {
        parentMotion(false);
        parentMotion(true);
        integratedHierarchy(1);
        integratedHierarchy(6);
        immediateHierarchy();
        std::cout << "[PASS] hierarchy transforms\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
