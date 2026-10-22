#include "test_support.h"

#include <functional>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <platformator/audio.h>
#include <platformator/animationclip.h>
#include <platformator/animator.h>
#include <platformator/assetreference.h>
#include <platformator/boxcollider.h>
#include <platformator/circlecollider.h>
#include <platformator/objectreference.h>
#include <platformator/rigidbody.h>
#include <platformator/scriptcomponent.h>

using platformator::Runtime;
using test_support::dt;
using test_support::require;

namespace
{
struct Counter : test_support::Behavior
{
    explicit Counter(int &value) : value(value)
    {
    }
    void update(double) override
    {
        ++value;
    }
    int &value;
};
struct Action : test_support::Behavior
{
    explicit Action(std::function<void()> action) : action(std::move(action))
    {
    }
    void update(double) override
    {
        if (!ran)
        {
            ran = true;
            action();
        }
    }
    bool ran = false;
    std::function<void()> action;
};
void noRuntime()
{
    ObjectReference<GameObject> absent(123);
    require(!absent && absent.resolve() == nullptr, "Unbound reference without a runtime must be empty");
}
void dispatchGrowth()
{
    int survivorUpdates = 0, additions = 0;
    Runtime runtime;
    auto *driver = runtime.createGameObject()->addComponent<ScriptComponent>();
    driver->getComponent<ScriptComponent>()->addBehavior(new Action([&] {
        for (int i = 0; i < 100; ++i)
            runtime.createGameObject()->addComponent<ScriptComponent>()->getComponent<ScriptComponent>()->addBehavior(
                new Counter(additions));
    }));
    runtime.createGameObject()->addComponent<ScriptComponent>()->getComponent<ScriptComponent>()->addBehavior(
        new Counter(survivorUpdates));
    runtime.simulateFrame(dt);
    require(survivorUpdates == 1 && additions == 0, "Dispatch must stay stable; additions start next step");
    runtime.simulateFrame(dt);
    require(survivorUpdates == 2 && additions == 100, "New scripts start before their first ordinary dispatch");
}
void logicalRetirement()
{
    int victimUpdates = 0, survivorUpdates = 0;
    Runtime runtime;
    auto *driver = runtime.createGameObject()->addComponent<ScriptComponent>();
    auto *victim = runtime.createGameObject()->addComponent<ScriptComponent>();
    auto *survivor = runtime.createGameObject()->addComponent<ScriptComponent>();
    driver->getComponent<ScriptComponent>()->addBehavior(new Action([&] { victim->destroy(); }));
    victim->getComponent<ScriptComponent>()->addBehavior(new Counter(victimUpdates));
    survivor->getComponent<ScriptComponent>()->addBehavior(new Counter(survivorUpdates));
    runtime.simulateFrame(dt);
    require(victimUpdates == 0 && survivorUpdates == 1, "Retirement must suppress victim without skipping survivor");
}
struct Teardown : test_support::Behavior
{
    explicit Teardown(int &count) : count(count)
    {
    }
    void onDestroy() override
    {
        auto *box = getGameObject()->getComponent<BoxCollider>();
        require(box && box->getWidth() == 20, "Attached collider must remain readable during onDestroy");
        ++count;
    }
    int &count;
};
void hierarchy()
{
    for (bool childFirst : {false, true})
    {
        int destroyed = 0;
        {
            Runtime runtime;
            auto *first = runtime.createGameObject();
            auto *second = runtime.createGameObject();
            auto *parent = childFirst ? second : first;
            auto *child = childFirst ? first : second;
            parent->addChild(child);
            for (auto *object : {parent, child})
                object->addComponent<BoxCollider>(20, 20)
                    ->addComponent<ScriptComponent>()
                    ->getComponent<ScriptComponent>()
                    ->addBehavior(new Teardown(destroyed));
        }
        require(destroyed == 2, "Flat ownership must destroy each child and parent once in either allocation order");
    }
}
void identities()
{
    ObjectReference<GameObject> old;
    {
        Runtime runtime;
        auto *object = runtime.createGameObject()->addComponent<CircleCollider>(10);
        old.set(object);
        ObjectReference<GameObject> wrong(object->getComponent<CircleCollider>()->getId());
        require(!wrong.resolve(runtime), "Binding a component ID as GameObject must reject the kind");
        ObjectReference<BoxCollider> wrongShape(object->getComponent<CircleCollider>()->getId());
        require(!wrongShape.resolve(runtime), "Box reference must reject circle ID");
        object->destroy();
        require(!old, "Reference must expire on logical retirement");
        runtime.simulateFrame(dt);
        runtime.createGameObject();
        require(!old.resolve(runtime), "Expired bound references must not rebind to reused slots/IDs");
    }
    require(!old && old.resolve() == nullptr, "Reference outliving its runtime must remain empty");
    Runtime recreated;
    recreated.createGameObject();
    require(!old, "New runtime must not revive an old handle");
}
void resourceRetention()
{
    AnimationClip clip;
    AssetReference<AnimationClip> retained(&clip);
    require(clip.referenceCount == 1, "Asset reference must retain clip");
    Runtime runtime;
    auto *animator = runtime.createGameObject()->addComponent<Animator>()->getComponent<Animator>();
    animator->setCurrentClip(&clip);
    require(clip.referenceCount == 2, "Animator must retain its clip");
    animator->setCurrentClip(&clip);
    require(clip.referenceCount == 2, "Same clip assignment must preserve ownership");
    runtime.clearScene();
    require(clip.referenceCount == 1, "Animator retirement must release its clip");
}
struct StartCounter : Counter
{
    StartCounter(int &updates, int &starts) : Counter(updates), starts(starts)
    {
    }
    void start() override
    {
        ++starts;
    }
    int &starts;
};
void activation()
{
    int updates = 0, starts = 0;
    Runtime runtime;
    auto *object = runtime.createGameObject()->setActive(false)->addComponent<ScriptComponent>();
    object->getComponent<ScriptComponent>()->addBehavior(new StartCounter(updates, starts));
    runtime.simulateFrame(dt);
    require(starts == 0 && updates == 0, "Inactive scripts must not start or update");
    object->setActive(true);
    runtime.simulateFrame(dt);
    require(starts == 1 && updates == 1, "Activation must start once before update");
    runtime.simulateFrameWithCustomCallback(dt, [&](double) {
        object->setActive(false);
        object->setActive(true);
    });
    require(starts == 1 && updates == 1, "Re-enabling within a step must wait for the boundary");
    runtime.simulateFrame(dt);
    require(starts == 1 && updates == 2, "Reactivation must restore eligibility without repeating start");
}
void callbackFailure()
{
    int updates = 0;
    Runtime runtime;
    auto *object = runtime.createGameObject()->addComponent<ScriptComponent>();
    ObjectReference<GameObject> reference(object);
    object->getComponent<ScriptComponent>()->addBehavior(new Action([&] {
        object->destroy();
        throw std::runtime_error("deliberate callback failure");
    }));
    bool raised = false;
    try
    {
        runtime.simulateFrame(dt);
    }
    catch (const std::runtime_error &)
    {
        raised = true;
    }
    require(raised && !reference, "Callback error must escape after mandatory retirement");
    runtime.createGameObject()->addComponent<ScriptComponent>()->getComponent<ScriptComponent>()->addBehavior(
        new Counter(updates));
    runtime.simulateFrame(dt);
    require(updates == 1, "Runtime phase must return to idle after callback failure");
}
void audioRetention()
{
    const auto path = std::filesystem::current_path() / "assets/test_runtime/foundation.wav";
    std::filesystem::create_directories(path.parent_path());
    {
        std::ofstream wav(path, std::ios::binary);
        auto integer = [&](uint32_t value, int bytes) {
            for (int i = 0; i < bytes; ++i)
                wav.put(static_cast<char>(value >> (8 * i)));
        };
        wav << "RIFF";
        integer(36 + 256, 4);
        wav << "WAVEfmt ";
        integer(16, 4);
        integer(1, 2);
        integer(1, 2);
        integer(8000, 4);
        integer(16000, 4);
        integer(2, 2);
        integer(16, 2);
        wav << "data";
        integer(256, 4);
        for (int i = 0; i < 256; ++i)
            wav.put(0);
    }
    {
        Runtime runtime;
        auto *cached = runtime.loadAudio(path.string());
        require(cached, "Audio fixture must load");
        AssetReference<AudioWrapper> retained(cached);
        auto *owner = runtime.createGameObject()->addComponent<Audio>(cached);
        owner->destroy();
        require(retained->getAudio() != nullptr, "Audio component must not release another owner's asset");
    }
    std::filesystem::remove(path);
}
} // namespace

int main(int argc, char **argv)
{
    test_support::headlessEnvironment();
    const std::pair<const char *, void (*)()> cases[] = {
        {"no_runtime", noRuntime},          {"growth", dispatchGrowth},
        {"retirement", logicalRetirement},  {"hierarchy", hierarchy},
        {"identity", identities},           {"resources", resourceRetention},
        {"activation", activation},         {"callback_failure", callbackFailure},
        {"audio_resources", audioRetention}};
    int failures = 0;
    for (auto [name, run] : cases)
    {
        if (argc > 1 && std::string(argv[1]) != name)
            continue;
        try
        {
            run();
            std::cout << "[PASS] " << name << '\n';
        }
        catch (const std::exception &error)
        {
            ++failures;
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
        }
    }
    return failures ? 1 : 0;
}
