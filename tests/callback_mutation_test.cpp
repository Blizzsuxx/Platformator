#include "test_support.h"
#include <functional>
#include <iostream>
#include <platformator/animator.h>
#include <platformator/boxcollider.h>
#include <platformator/objectreference.h>
#include <platformator/scriptcomponent.h>

using namespace test_support;
namespace
{
enum class Phase
{
    Start,
    Fixed,
    Update,
    Late,
    Enter,
    Stay,
    Exit
};
struct Counts
{
    int starts = 0, fixed = 0, updates = 0, late = 0, destroyed = 0;
};
struct Counter : test_support::Behavior
{
    explicit Counter(Counts &counts) : counts(counts)
    {
    }
    void start() override
    {
        ++counts.starts;
    }
    void fixedUpdate(double) override
    {
        ++counts.fixed;
    }
    void update(double) override
    {
        ++counts.updates;
    }
    void lateUpdate(double) override
    {
        ++counts.late;
    }
    void onDestroy() override
    {
        ++counts.destroyed;
    }
    Counts &counts;
};
struct Action : test_support::Behavior
{
    Action(Phase phase, std::function<void()> action) : phase(phase), action(std::move(action))
    {
    }
    void fire(Phase current)
    {
        if (phase == current && !ran)
        {
            ran = true;
            action();
        }
    }
    void start() override
    {
        fire(Phase::Start);
    }
    void fixedUpdate(double) override
    {
        fire(Phase::Fixed);
    }
    void update(double) override
    {
        fire(Phase::Update);
    }
    void lateUpdate(double) override
    {
        fire(Phase::Late);
    }
    void onCollisionEnter(const Collision *, Collider *, double) override
    {
        fire(Phase::Enter);
    }
    void onCollisionStay(const Collision *, Collider *, double) override
    {
        fire(Phase::Stay);
    }
    void onCollisionExit(Collider *, double) override
    {
        fire(Phase::Exit);
    }
    Phase phase;
    std::function<void()> action;
    bool ran = false;
};
void replaceDuring(Phase phase)
{
    Counts oldCounts, replacementCounts, survivorCounts;
    platformator::Runtime runtime;
    auto *owner = runtime.createGameObject()->addComponent<ScriptComponent>();
    auto *old = owner->getComponent<ScriptComponent>();
    ObjectReference<ScriptComponent> retired(old);
    bool replaced = false;
    old->addBehavior(new Action(phase, [&] {
        owner->addComponent<ScriptComponent>();
        auto *replacement = owner->getComponent<ScriptComponent>();
        require(replacement != old && !retired, "Replacement slot and reference retirement must be immediate");
        replacement->addBehavior(new Counter(replacementCounts));
        require(old->getGameObject() == owner, "Executing replaced script allocation must remain readable");
        replaced = true;
    }));
    old->addBehavior(new Counter(oldCounts));
    runtime.createGameObject()->addComponent<ScriptComponent>()->getComponent<ScriptComponent>()->addBehavior(
        new Counter(survivorCounts));
    GameObject *other = nullptr;
    if (phase == Phase::Enter || phase == Phase::Stay || phase == Phase::Exit)
    {
        owner->addComponent<BoxCollider>(20, 20);
        other = runtime.createGameObject()->addComponent<BoxCollider>(20, 20);
    }
    runtime.simulateFrame(dt);
    if (phase == Phase::Stay || phase == Phase::Exit)
    {
        other->setPosition({phase == Phase::Stay ? 0.1f : 1000.f, 0});
        runtime.simulateFrame(dt);
    }
    require(replaced && oldCounts.destroyed == 1 && replacementCounts.starts == 0,
            "Self replacement must retire once and defer new starts");
    if (phase == Phase::Start)
        require(oldCounts.starts == 0, "Replacement must cancel remaining starts");
    if (phase == Phase::Update || phase == Phase::Enter)
        require(oldCounts.updates == 0 && oldCounts.late == 0, "Remaining behaviors/phases must skip replaced script");
    const auto previous = survivorCounts.updates;
    runtime.simulateFrame(dt);
    require(replacementCounts.starts == 1 && replacementCounts.updates == 1 && survivorCounts.updates == previous + 1,
            "Replacement starts once and dense-list removal cannot skip survivors");
}
void removeAndToggle()
{
    Counts counts;
    platformator::Runtime runtime;
    auto *owner = runtime.createGameObject()->addComponent<ScriptComponent>();
    owner->getComponent<ScriptComponent>()->addBehavior(new Action(Phase::Fixed, [&] {
        require(owner->removeComponent(SCRIPT) && !owner->getComponent<ScriptComponent>(),
                "Removal detaches the slot immediately");
    }));
    owner->getComponent<ScriptComponent>()->addBehavior(new Counter(counts));
    runtime.simulateFrame(dt);
    require(counts.fixed == 0 && counts.updates == 0 && counts.destroyed == 1,
            "Self removal suppresses remaining dispatch");
    owner->setActive(false)->addComponent<ScriptComponent>();
    owner->getComponent<ScriptComponent>()->addBehavior(new Counter(counts));
    runtime.simulateFrame(dt);
    require(counts.starts == 1, "Inactive scripts cannot start");
    owner->setActive(true);
    runtime.simulateFrame(dt);
    require(counts.starts == 2 && counts.updates == 1, "Activation starts before ordinary dispatch");
    runtime.simulateFrameWithCustomCallback(dt, [&](double) {
        for (int i = 0; i < 10; ++i)
            owner->setActive(false)->setActive(true);
    });
    require(counts.updates == 1, "Re-enable cannot rejoin the running dispatch");
    runtime.simulateFrame(dt);
    require(counts.starts == 2 && counts.updates == 2, "Coalesced toggles preserve membership and start once");
}
void growAndDestroy()
{
    Counts additions, victimCounts, survivorCounts;
    platformator::Runtime runtime;
    auto *driver = runtime.createGameObject()->addComponent<ScriptComponent>();
    auto *victim = runtime.createGameObject()->addComponent<ScriptComponent>();
    auto *survivor = runtime.createGameObject()->addComponent<ScriptComponent>();
    driver->getComponent<ScriptComponent>()->addBehavior(new Action(Phase::Update, [&] {
        victim->destroy();
        victim->destroy();
        for (int i = 0; i < 100; ++i)
            runtime.createGameObject()->addComponent<ScriptComponent>()->getComponent<ScriptComponent>()->addBehavior(
                new Counter(additions));
    }));
    victim->getComponent<ScriptComponent>()->addBehavior(new Counter(victimCounts));
    survivor->getComponent<ScriptComponent>()->addBehavior(new Counter(survivorCounts));
    runtime.simulateFrame(dt);
    require(victimCounts.updates == 0 && victimCounts.destroyed == 1 && survivorCounts.updates == 1 &&
                additions.updates == 0,
            "Destroy/growth must preserve exactly-once survivor dispatch");
    runtime.simulateFrame(dt);
    require(additions.starts == 100 && additions.updates == 100 && survivorCounts.updates == 2,
            "Additions join next step");
}
void animationMutation()
{
    AnimationClip first({AnimationFrame(nullptr, 0)}, 120, false, 0, 0, "first");
    AnimationClip second({AnimationFrame(nullptr, 0)}, 120, false, 0, 0, "second");
    int callbacks = 0, appended = 0;
    platformator::Runtime runtime;
    auto *owner = runtime.createGameObject()->addComponent<Animator>();
    auto *animator = owner->getComponent<Animator>();
    animator->addOnClipEndCallback([&] {
        ++callbacks;
        for (int i = 0; i < 100; ++i)
            animator->addOnClipEndCallback([&] { ++appended; });
        animator->play(&second);
    });
    animator->addOnClipEndCallback([&] {
        ++callbacks;
        owner->removeComponent(ANIMATOR);
    });
    animator->addOnClipEndCallback([&] { throw std::runtime_error("Callback after retirement must be suppressed"); });
    animator->play(&first);
    runtime.simulateFrame(dt);
    require(callbacks == 2 && appended == 0 && !owner->getComponent<Animator>(),
            "Animation callbacks need stable storage and retirement gates");
}
} // namespace
int main()
{
    headlessEnvironment();
    try
    {
        for (auto phase :
             {Phase::Start, Phase::Fixed, Phase::Update, Phase::Late, Phase::Enter, Phase::Stay, Phase::Exit})
            replaceDuring(phase);
        removeAndToggle();
        growAndDestroy();
        animationMutation();
        std::cout << "[PASS] callback mutation contracts\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
