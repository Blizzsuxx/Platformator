#include "test_support.h"
#include <iostream>
#include <platformator/animationclip.h>
#include <platformator/animator.h>
#include <platformator/boxcollider.h>
#include <platformator/objectreference.h>
#include <platformator/scriptcomponent.h>

using namespace test_support;

namespace
{
enum class Family
{
    Start,
    Fixed,
    Update,
    Late,
    Enter,
    Stay,
    Exit,
    Animation
};
struct Transition : test_support::Behavior
{
    Transition(Family family, const std::string &path, int &requests, int &destroyed)
        : family(family), path(path), requests(requests), destroyed(destroyed)
    {
    }
    void fire(Family current)
    {
        if (family != current || requested)
            return;
        requested = true;
        getRuntime().requestLoadScene(path);
        require(getGameObject()->getName() == "driver", "A scene request must return to a live caller");
        ++requests;
    }
    void start() override
    {
        fire(Family::Start);
    }
    void fixedUpdate(double) override
    {
        fire(Family::Fixed);
    }
    void update(double) override
    {
        fire(Family::Update);
    }
    void lateUpdate(double) override
    {
        fire(Family::Late);
    }
    void onCollisionEnter(const Collision *, Collider *, double) override
    {
        fire(Family::Enter);
    }
    void onCollisionStay(const Collision *, Collider *, double) override
    {
        fire(Family::Stay);
    }
    void onCollisionExit(Collider *, double) override
    {
        fire(Family::Exit);
    }
    void onDestroy() override
    {
        ++destroyed;
    }
    Family family;
    std::string path;
    int &requests, &destroyed;
    bool requested = false;
};
} // namespace

int main()
{
    headlessEnvironment();
    const auto a = std::filesystem::absolute("stress-a.scene");
    const auto b = std::filesystem::absolute("stress-b.scene");
    try
    {
        writeScene(a, "old");
        writeScene(b, "new");
        AnimationClip clip({AnimationFrame(nullptr, 0)}, 120, false, 0, 0, "once");
        int requests = 0, destroyed = 0;
        platformator::Runtime runtime;
        for (int iteration = 0; iteration < 1000; ++iteration)
        {
            runtime.loadScene(a.string());
            auto family = static_cast<Family>(iteration % 8);
            auto *driver = runtime.createGameObject()->setName("driver")->addComponent<ScriptComponent>();
            auto *behavior = new Transition(family, b.string(), requests, destroyed);
            driver->getComponent<ScriptComponent>()->addBehavior(behavior);
            ObjectReference<GameObject> expired(driver);
            GameObject *other = nullptr;
            if (family == Family::Enter || family == Family::Stay || family == Family::Exit)
            {
                driver->addComponent<BoxCollider>(20, 20);
                other = runtime.createGameObject()->addComponent<BoxCollider>(20, 20);
            }
            if (family == Family::Animation)
            {
                driver->addComponent<Animator>();
                auto *animator = driver->getComponent<Animator>();
                animator->addOnClipEndCallback([behavior] { behavior->fire(Family::Animation); });
                animator->play(&clip);
            }
            runtime.simulateFrame(dt);
            if (family == Family::Stay || family == Family::Exit)
            {
                other->setPosition(Eigen::Vector2f(family == Family::Stay ? 0.1f : 1000.0f, 0));
                runtime.simulateFrame(dt);
            }
            require(!expired && runtime.getGameObject("new") && !runtime.getGameObject("old"),
                    "Scripted replacement failed at iteration " + std::to_string(iteration));
            require(requests == iteration + 1 && destroyed == iteration + 1,
                    "Transition callbacks must run exactly once");
        }
        runtime.clearScene();
        require(runtime.getAllGameObjects().empty(), "Final clear must release the last scene");
        std::filesystem::remove(a);
        std::filesystem::remove(b);
        std::cout << "[PASS] 1000 scene transitions across eight callback families\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
