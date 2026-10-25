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
    void onCollisionExit(Collider *, double) override
    {
        fire(Phase::Exit);
    }
    Phase phase;
    std::function<void()> action;
    bool ran = false;
};
struct StartupCounts : Counts
{
    int resolutions = 0;
};
struct StartupProbe : Counter
{
    enum class Failure
    {
        None,
        Resolve,
        Start,
        Unknown
    };
    StartupProbe(StartupCounts &counts, Failure failure = Failure::None)
        : Counter(counts), startupCounts(counts), failure(failure)
    {
    }
    void resolveReferences(platformator::ReferenceResolver &) override
    {
        ++startupCounts.resolutions;
        if (failure == Failure::Resolve)
            throw std::runtime_error("resolve failure");
    }
    void start() override
    {
        Counter::start();
        if (failure == Failure::Start)
            throw std::runtime_error("start failure");
        if (failure == Failure::Unknown)
            throw 42;
    }
    StartupCounts &startupCounts;
    Failure failure;
};
void startupFailures()
{
    StartupCounts badReference, badStart, failedOnly, unknown, first, second, survivor, added;
    platformator::Runtime runtime;
    auto *owner = runtime.createGameObject()->addComponent<ScriptComponent>();
    auto *script = owner->getComponent<ScriptComponent>();
    auto *unresolved = script->addBehavior(new StartupProbe(badReference, StartupProbe::Failure::Resolve));
    script->addBehavior(new StartupProbe(first));
    script->addBehavior(new StartupProbe(badStart, StartupProbe::Failure::Start));
    script->addBehavior(new StartupProbe(second));
    require(script->getBehaviors().empty() && script->getPendingBehaviors().size() == 4 &&
                script->getBehavior<StartupProbe>() == unresolved,
            "Pending behaviors remain discoverable without joining ordinary dispatch");
    auto *failedOwner = runtime.createGameObject()->addComponent<ScriptComponent>();
    auto *failedScript = failedOwner->getComponent<ScriptComponent>();
    failedScript->addBehavior(new StartupProbe(failedOnly, StartupProbe::Failure::Start));
    failedScript->addBehavior(new StartupProbe(unknown, StartupProbe::Failure::Unknown));
    runtime.createGameObject()->addComponent<ScriptComponent>()->getComponent<ScriptComponent>()->addBehavior(
        new StartupProbe(survivor));
    runtime.simulateFrame(dt);
    require(first.starts == 1 && second.starts == 1 && survivor.starts == 1 && badStart.starts == 1 &&
                badReference.starts == 0 && failedOnly.starts == 1 && unknown.starts == 1,
            "Startup errors must not escape or prevent other initialization attempts");
    require(script->getBehaviors().size() == 4 && failedScript->getBehaviors().size() == 2 &&
                script->getPendingBehaviors().empty() && failedScript->getPendingBehaviors().empty() &&
                script->getBehavior<StartupProbe>() == unresolved,
            "All attempted behaviors become runnable and remain discoverable, including failed initialization");
    require(badReference.destroyed == 0 && badStart.destroyed == 0 && failedOnly.destroyed == 0 && unknown.destroyed == 0,
            "Startup errors must not destroy behaviors");
    require(first.fixed == 1 && first.updates == 1 && first.late == 1 && second.updates == 1 && survivor.updates == 1 &&
                badReference.fixed == 1 && badReference.updates == 1 && badReference.late == 1 && badStart.updates == 1 &&
                failedOnly.updates == 1 && unknown.updates == 1,
            "Failed initialization still participates in ordinary callbacks in the same step");
    owner->setActive(false)->setActive(true);
    failedOwner->setActive(false)->setActive(true);
    script->addBehavior(new StartupProbe(added));
    runtime.simulateFrame(dt);
    require(first.starts == 1 && second.starts == 1 && added.starts == 1 && added.updates == 1 &&
                badReference.resolutions == 1 && badStart.resolutions == 1 && failedOnly.resolutions == 1 &&
                unknown.resolutions == 1 && badReference.starts == 0 && badStart.starts == 1 &&
                failedOnly.starts == 1 && unknown.starts == 1 && badReference.updates == 2,
            "Reactivation and later additions must not retry initialization");
    require(script->getBehaviors().size() == 5 && failedScript->getBehaviors().size() == 2,
            "Logged errors must leave the original behaviors owned until normal teardown");
    runtime.clearScene();
    require(badReference.destroyed == 1 && badStart.destroyed == 1 && failedOnly.destroyed == 1 && unknown.destroyed == 1 &&
                first.destroyed == 1,
            "Normal scene teardown destroys each retained behavior once");
}
struct StartOrder : test_support::Behavior
{
    StartOrder(std::vector<int> &trace, int id) : trace(trace), id(id)
    {
    }
    void start() override
    {
        trace.push_back(id);
    }
    std::vector<int> &trace;
    int id;
};
void scriptStartupQueue()
{
    std::vector<int> trace;
    Counts deferred;
    platformator::Runtime runtime;
    auto *first = runtime.createGameObject()->addComponent<ScriptComponent>()->getComponent<ScriptComponent>();
    auto *second = runtime.createGameObject()->addComponent<ScriptComponent>()->getComponent<ScriptComponent>();
    first->addBehavior(new StartOrder(trace, 1));
    second->addBehavior(new StartOrder(trace, 2));
    first->addBehavior(new StartOrder(trace, 3));
    runtime.simulateFrame(dt);
    require(trace == std::vector<int>{1, 3, 2}, "Queued scripts start their pending behaviors together, once each");
    auto *deferredOwner = runtime.createGameObject()->addComponent<ScriptComponent>();
    first->addBehavior(new Action(Phase::Start, [&] { deferredOwner->setActive(false); }));
    deferredOwner->getComponent<ScriptComponent>()->addBehavior(new Counter(deferred));
    runtime.simulateFrame(dt);
    require(deferred.starts == 0 && deferred.updates == 0 &&
                deferredOwner->getComponent<ScriptComponent>()->getPendingBehaviors().size() == 1,
            "Deactivation during the batch must preserve unattempted pending behaviors");
    deferredOwner->setActive(true);
    runtime.simulateFrame(dt);
    require(deferred.starts == 1 && deferred.updates == 1, "Activation queues only remaining pending work");
    require(first->getPendingBehaviors().empty() && second->getPendingBehaviors().empty(),
            "Completed scripts leave no unattempted prefix behind");
    Counts interrupted;
    auto *interruptedOwner = runtime.createGameObject()->addComponent<ScriptComponent>();
    auto *interruptedScript = interruptedOwner->getComponent<ScriptComponent>();
    interruptedScript->addBehavior(new Action(Phase::Start, [&] { interruptedOwner->setActive(false); }));
    interruptedScript->addBehavior(new Counter(interrupted));
    runtime.simulateFrame(dt);
    require(interrupted.starts == 0 && interruptedScript->getBehaviors().size() == 1 &&
                interruptedScript->getPendingBehaviors().size() == 1,
            "Interrupted startup transfers only attempted entries and preserves the unprocessed suffix");
    interruptedOwner->setActive(true);
    runtime.simulateFrame(dt);
    require(interrupted.starts == 1 && interrupted.updates == 1 && interruptedScript->getPendingBehaviors().empty(),
            "Activation starts the retained suffix once");
}
void additionsWithinActiveScripts()
{
    Counts existing, ownStart, otherStart, earlierStart, ownFixed, otherFixed;
    platformator::Runtime runtime;
    auto *owner = runtime.createGameObject()->addComponent<ScriptComponent>();
    auto *other = runtime.createGameObject()->addComponent<ScriptComponent>();
    auto *script = owner->getComponent<ScriptComponent>();
    auto *otherScript = other->getComponent<ScriptComponent>();
    script->addBehavior(new Action(Phase::Start, [&] {
        script->addBehavior(new Counter(ownStart));
        otherScript->addBehavior(new Counter(otherStart));
    }));
    otherScript->addBehavior(new Counter(existing));
    otherScript->addBehavior(new Action(Phase::Start, [&] {
        script->addBehavior(new Counter(earlierStart));
    }));
    runtime.simulateFrame(dt);
    require(existing.starts == 1 && existing.updates == 1 && ownStart.starts == 1 && ownStart.updates == 1 &&
                otherStart.starts == 1 && otherStart.updates == 1 && earlierStart.starts == 1 && earlierStart.updates == 1,
            "Startup drains additions to current, later queued, and already processed scripts");
    require(script->getPendingBehaviors().empty() && otherScript->getPendingBehaviors().empty(),
            "Requeued already-processed scripts drain their new pending work in the same startup pass");
    runtime.simulateFrame(dt);
    require(ownStart.starts == 1 && ownStart.updates == 2 && otherStart.starts == 1 && otherStart.updates == 2 &&
                earlierStart.starts == 1 && earlierStart.updates == 2,
            "Behaviors added during startup initialize once before ordinary dispatch");
    script->addBehavior(new Action(Phase::Fixed, [&] {
        script->addBehavior(new Counter(ownFixed));
        otherScript->addBehavior(new Counter(otherFixed));
    }));
    runtime.simulateFrame(dt);
    require(ownFixed.starts == 0 && ownFixed.fixed == 0 && ownFixed.updates == 0 && ownFixed.late == 0 &&
                otherFixed.starts == 0 && otherFixed.fixed == 0 && otherFixed.updates == 0 && otherFixed.late == 0,
            "Pending additions cannot receive later-phase callbacks before startup");
    runtime.simulateFrame(dt);
    require(ownFixed.starts == 1 && ownFixed.fixed == 1 && ownFixed.updates == 1 && ownFixed.late == 1 &&
                otherFixed.starts == 1 && otherFixed.updates == 1,
            "Promoted behaviors initialize once before ordinary callbacks");
}
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
    if (phase == Phase::Enter || phase == Phase::Exit)
    {
        owner->addComponent<BoxCollider>(20, 20);
        other = runtime.createGameObject()->addComponent<BoxCollider>(20, 20);
    }
    runtime.simulateFrame(dt);
    if (phase == Phase::Exit)
    {
        other->setPosition({1000.f, 0});
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
        for (auto phase : {Phase::Start, Phase::Fixed, Phase::Update, Phase::Late, Phase::Enter, Phase::Exit})
            replaceDuring(phase);
        removeAndToggle();
        growAndDestroy();
        startupFailures();
        scriptStartupQueue();
        additionsWithinActiveScripts();
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
