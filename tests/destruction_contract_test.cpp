#include "test_support.h"
#include <functional>
#include <iostream>
#include <platformator/boxcollider.h>
#include <platformator/objectreference.h>
#include <platformator/scriptcomponent.h>

using namespace test_support;
namespace
{
struct Teardown : test_support::Behavior
{
    explicit Teardown(std::function<void(GameObject *)> action) : action(std::move(action))
    {
    }
    void onDestroy() override
    {
        action(getGameObject());
    }
    std::function<void(GameObject *)> action;
};
void checkRejections(GameObject *owner, GameObject *other, int &rejections)
{
    auto &runtime = platformator::Runtime::current();
    const auto size = runtime.getAllGameObjects().size();
    const auto active = other->getActive();
    const auto parent = other->getParent();
    const auto box = other->getComponent<BoxCollider>();
    const std::function<void()> operations[] = {[&] { runtime.createGameObject(); },
                                                [&] { other->destroy(); },
                                                [&] { other->setActive(false); },
                                                [&] { other->addComponent<BoxCollider>(10, 10); },
                                                [&] { other->removeComponent(COLLIDER); },
                                                [&] { other->addChild(owner); },
                                                [&] { runtime.clearScene(); },
                                                [&] { runtime.requestLoadScene("unused.scene"); },
                                                [&] { runtime.requestClearScene(); },
                                                [&] { runtime.createMainCameraIfNoMainCameraExists(); }};
    for (const auto &operation : operations)
    {
        bool rejected = false;
        try
        {
            operation();
        }
        catch (const std::logic_error &)
        {
            rejected = true;
        }
        require(rejected, "Structural work must reject during onDestroy/shutdown");
        ++rejections;
    }
    require(runtime.getAllGameObjects().size() == size && other->getActive() == active &&
                other->getParent() == parent && other->getComponent<BoxCollider>() == box,
            "Rejected teardown operations cannot change state");
}
void hierarchy(bool childFirst, bool shutdown)
{
    int calls = 0, rejected = 0;
    {
        platformator::Runtime runtime;
        auto *first = runtime.createGameObject();
        auto *second = runtime.createGameObject();
        auto *parent = childFirst ? second : first;
        auto *child = childFirst ? first : second;
        parent->setPosition({10, 20});
        child->setPosition({30, 40});
        parent->addChild(child);
        require(child->getPosition() == Eigen::Vector2f(30, 40), "Reparent must preserve world translation");
        bool rejectedCycle = false;
        try
        {
            child->addChild(parent);
        }
        catch (const std::invalid_argument &)
        {
            rejectedCycle = true;
        }
        require(rejectedCycle && !parent->getParent() && child->getParent() == parent,
                "Cycles reject before editing links");
        for (auto *object : {parent, child})
            object->addComponent<BoxCollider>(20, 20)
                ->addComponent<ScriptComponent>()
                ->getComponent<ScriptComponent>()
                ->addBehavior(new Teardown([&](GameObject *owner) {
                    require(owner->getComponent<BoxCollider>()->getWidth() == 20,
                            "Attached components remain readable during teardown");
                    checkRejections(owner, owner == parent ? child : parent, rejected);
                    ++calls;
                }));
        if (!shutdown)
        {
            runtime.simulateFrameWithCustomCallback(dt, [&](double) {
                parent->destroy();
                parent->destroy();
            });
            require(calls == 2 && rejected == 20,
                    "Subtree destroy must deliver exactly once before freeing either allocation");
        }
    }
    require(calls == 2 && rejected == 20, "Shutdown must obey the same readable teardown contract");
}
void slotsAndFailure()
{
    int removed = 0, replaced = 0, cleaned = 0;
    platformator::Runtime runtime;
    auto *owner = runtime.createGameObject()->addComponent<ScriptComponent>();
    owner->getComponent<ScriptComponent>()->addBehavior(new Teardown([&](GameObject *object) {
        require(!object->getComponent<ScriptComponent>(), "Removed script stays absent during onDestroy");
        ++removed;
    }));
    owner->removeComponent(SCRIPT);
    auto *second = runtime.createGameObject()->addComponent<ScriptComponent>();
    auto *oldScript = second->getComponent<ScriptComponent>();
    oldScript->addBehavior(new Teardown([&, oldScript](GameObject *object) {
        require(object->getComponent<ScriptComponent>() != oldScript, "onDestroy must observe the replacement slot");
        ++replaced;
        throw std::runtime_error("intentional teardown failure");
    }));
    oldScript->addBehavior(new Teardown([&](GameObject *) { ++cleaned; }));
    ObjectReference<ScriptComponent> expired(oldScript);
    bool failed = false;
    try
    {
        second->addComponent<ScriptComponent>();
    }
    catch (const std::runtime_error &)
    {
        failed = true;
    }
    require(removed == 1 && replaced == 1 && cleaned == 1 && failed && !expired,
            "Callback failure must finish the entire retirement batch");
    runtime.simulateFrame(dt);
    require(second->getComponent<ScriptComponent>() && cleaned == 1,
            "Cleanup restores idle without repeating teardown");
}
} // namespace
int main()
{
    headlessEnvironment();
    try
    {
        for (bool order : {false, true})
            for (bool shutdown : {false, true})
                hierarchy(order, shutdown);
        slotsAndFailure();
        std::cout << "[PASS] destruction contracts\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
