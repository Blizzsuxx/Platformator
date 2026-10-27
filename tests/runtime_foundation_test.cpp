#include "runtimeaccess.h"
#include "buildconfig.h"
#include "test_support.h"

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <platformator/animationclip.h>
#include <platformator/animator.h>
#include <platformator/assetreference.h>
#include <platformator/audio.h>
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
    bool rejected = false;
    try
    {
        platformator_detail::RuntimeAccess::gameManager();
    }
    catch (const std::logic_error &error)
    {
        rejected = std::string(error.what()) == "Platformator runtime is not active.";
    }
    require(rejected && Runtime::tryGetCurrent() == nullptr,
            "Privileged manager access without a runtime rejects with the established error");
}

struct AccessOnDestroy : test_support::Behavior
{
    explicit AccessOnDestroy(std::function<void()> action) : action(std::move(action))
    {
    }

    void onDestroy() override
    {
        action();
    }

    std::function<void()> action;
};

void runtimeAccessLifetime()
{
    bool observed = false;
    {
        Runtime runtime;
        auto *manager = &platformator_detail::RuntimeAccess::gameManager();
        runtime.createGameObject()
            ->addComponent<ScriptComponent>()
            ->getComponent<ScriptComponent>()
            ->addBehavior(new AccessOnDestroy([&, manager] {
                require(&platformator_detail::RuntimeAccess::runtime() == &runtime &&
                            &platformator_detail::RuntimeAccess::gameManager() == manager,
                        "Runtime and manager remain accessible throughout shutdown callbacks");
                require(runtime.getRuntimeSerial() != 0,
                        "Shutdown callback can read the live identity registry");
                observed = true;
            }));
    }
    require(observed, "Shutdown must deliver the access observation");
    noRuntime();
}

void dispatchGrowth()
{
    int survivorUpdates = 0, additions = 0;
    Runtime runtime;
    auto *driver = runtime.createGameObject()->addComponent<ScriptComponent>();
    driver->getComponent<ScriptComponent>()->addBehavior(new Action([&] {
        for (int i = 0; i < 100; ++i)
        {
            runtime.createGameObject()
                ->addComponent<ScriptComponent>()
                ->getComponent<ScriptComponent>()
                ->addBehavior(new Counter(additions));
        }
    }));
    runtime.createGameObject()->addComponent<ScriptComponent>()->getComponent<ScriptComponent>()->addBehavior(
        new Counter(survivorUpdates));
    runtime.simulateFrame(dt);
    require(survivorUpdates == 1 && additions == 0, "Dispatch must stay stable; additions start next step");
    runtime.simulateFrame(dt);
    require(survivorUpdates == 2 && additions == 100,
            "New scripts start before their first ordinary dispatch");
}

void logicalRetirement()
{
    int victimUpdates = 0, survivorUpdates = 0;
    Runtime runtime;
    auto *driver = runtime.createGameObject()->addComponent<ScriptComponent>();
    auto *victim = runtime.createGameObject()->addComponent<ScriptComponent>();
    auto *survivor = runtime.createGameObject()->addComponent<ScriptComponent>();
    driver->getComponent<ScriptComponent>()->addBehavior(new Action([&] {
        victim->destroy();
    }));
    victim->getComponent<ScriptComponent>()->addBehavior(new Counter(victimUpdates));
    survivor->getComponent<ScriptComponent>()->addBehavior(new Counter(survivorUpdates));
    runtime.simulateFrame(dt);
    require(victimUpdates == 0 && survivorUpdates == 1,
            "Retirement must suppress victim without skipping survivor");
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
            {
                object->addComponent<BoxCollider>(20, 20)
                    ->addComponent<ScriptComponent>()
                    ->getComponent<ScriptComponent>()
                    ->addBehavior(new Teardown(destroyed));
            }
        }
        require(destroyed == 2,
                "Flat ownership must destroy each child and parent once in either allocation order");
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

constexpr const char *audioPath = "assets/audio/jump.wav";
constexpr const char *otherAudioPath = "assets/audio/missing.wav";

bool waitForAudio(const std::function<bool()> &complete)
{
    const auto deadline = SDL_GetTicks() + 2000;
    do
    {
        if (complete())
            return true;
        SDL_Delay(5);
    } while (SDL_GetTicks() < deadline);
    return false;
}

void audioRetention()
{
    {
        Runtime runtime;
        auto *cached = runtime.loadAudio(audioPath);
        require(cached, "Audio fixture must load");
        AssetReference<AudioWrapper> retained(cached);
        auto *owner = runtime.createGameObject()->addComponent<Audio>(cached);
        owner->destroy();
        require(retained->getAudio() != nullptr, "Audio component must not release another owner's asset");
    }
    {
        Runtime runtime;
        auto *cached = runtime.loadAudio(audioPath);
        auto *owner = runtime.createGameObject()->addComponent<Audio>(cached);
        auto *audio = owner->getComponent<Audio>();
        audio->setLoopCount(-1);
        require(audio->playAndForget(), "Transient playback must start");
        owner->destroy();
        require(runtime.loadAudio(audioPath) == cached,
                "Transient playback must retain its audio after component destruction");
        cached->addReference();
        runtime.stopTransientAudio();
        require(cached->removeReferenceAndFreeIfNoReferences(),
                "Stopping transient audio must release the looping playback's resource reference");
        runtime.simulateFrame(dt);
    }
}

void audioTransientCleanup()
{
    RuntimeOptions options;
    options.debugSettings.startPaused = true;
    Runtime runtime(options);
    auto *cached = runtime.loadAudio(audioPath);
    cached->addReference();
    auto *owner = runtime.createGameObject()->addComponent<Audio>(cached);
    require(owner->getComponent<Audio>()->playAndForget(), "Finite transient playback must start");
    owner->destroy();
    int steps = 0;
    require(waitForAudio([&] {
        runtime.simulateAndRenderFrameWithCustomCallback(dt, [&](double) { ++steps; });
        // Release our probe reference, restoring it while playback still retains the asset.
        if (cached->removeReferenceAndFreeIfNoReferences())
            return true;
        cached->addReference();
        return false;
    }), "Runtime frames must release completed transient audio");
#if PLATFORMATOR_ENABLE_DEBUG_TOOLS
    require(steps == 0, "Paused audio cleanup must not advance simulation");
#endif
}

void audioAutoplay()
{
    Runtime runtime;
    auto *cached = runtime.loadAudio(audioPath);
    auto *owner = runtime.createGameObject()->setActive(false)->addComponent<Audio>(cached, 1.0f, true, -1.0f);
    auto *audio = owner->getComponent<Audio>();
    require(!audio->isPlaying(), "Autoplay must wait for an inactive owner to become active");
    auto *pathOwner = runtime.createGameObject()->setActive(false)->addComponent<Audio>(audioPath, 1.0f, true, -1.0f);
    require(!pathOwner->getComponent<Audio>()->isPlaying(), "Path-based autoplay also waits for activation");
    owner->setActive(true);
    require(audio->isPlaying(), "Activation starts autoplay");
    owner->setActive(false);
    require(!audio->isPlaying(), "Deactivation stops component playback");
    owner->setActive(true);
    require(audio->isPlaying(), "Reactivation restarts autoplay");
    require(audio->playAndForget(), "Independent looping playback must start");
    runtime.stopTransientAudio();
    require(audio->isPlaying(), "Stopping transient tracks must not stop the component track");
}

void checkAudioReplayCompletion(bool paused)
{
    RuntimeOptions options;
    options.debugSettings.startPaused = paused;
    Runtime runtime(options);
    auto *original = runtime.loadAudio(audioPath);
    auto *next = runtime.loadAudio(otherAudioPath);
    require(original && next && original != next, "Replay requires two distinct audio assets");
    auto *owner = runtime.createGameObject()->addComponent<Audio>(original, 1.0f, true, -1.0f);
    auto *audio = owner->getComponent<Audio>();
    require(audio->replay(next, 800), "Replay must schedule a fade-out");
    require(audio->getAudioWrapper() == original, "The original sound stays bound until the fade finishes");
    int steps = 0;
    require(waitForAudio([&] {
        if (paused)
            runtime.simulateAndRenderFrameWithCustomCallback(dt, [&](double) { ++steps; });
        else
            runtime.simulateFrame(dt);
        return audio->getAudioWrapper() == next && audio->isPlaying();
    }), "Replay must start the distinct target after fading");
    require(MIX_GetTrackAudio(audio->getTrack()) == next->getAudio(), "The mixer must use the replacement asset");
    require(MIX_GetTrackFadeFrames(audio->getTrack()) == 0, "The restarted track is no longer fading");
#if PLATFORMATOR_ENABLE_DEBUG_TOOLS
    if (paused)
        require(steps == 0, "Paused replay must advance without a simulation step");
#endif
}

void audioReplayCompletion()
{
    checkAudioReplayCompletion(false);
}

void audioReplayWhilePaused()
{
    checkAudioReplayCompletion(true);
}

void audioReplayCancellation()
{
    Runtime runtime;
    AssetReference<AudioWrapper> original(runtime.loadAudio(audioPath));
    auto *owner = runtime.createGameObject()->addComponent<Audio>(original.get(), 1.0f, true, -1.0f);
    auto *audio = owner->getComponent<Audio>();
    require(audio->replay(original.get(), 480000), "Replay can queue the current sound");
    require(MIX_GetTrackFadeFrames(audio->getTrack()) < 0, "Replay retains the requested fade-out");
    require(audio->stop(), "Stopping cancels a pending replay");
    runtime.simulateFrame(dt);
    require(!audio->isPlaying(), "A cancelled replay must not restart");
    require(audio->play(), "Playback can restart after cancellation");
    auto *next = runtime.loadAudio(otherAudioPath);
    require(audio->replay(next, 480000), "A different sound can be queued for replay");
    require(audio->replay(next, 480000), "Replacing a pending replay retains its target");
    audio->setAudio(next);
    runtime.simulateFrame(dt);
    require(audio->getAudioWrapper() == next, "Setting a pending target keeps it alive");
    require(audio->replay(original.get(), 480000), "Replay must schedule before deactivation");
    owner->setActive(false);
    runtime.simulateFrame(dt);
    require(!audio->isPlaying() && audio->getAudioWrapper() == next, "Deactivation cancels a pending replay");
    owner->setActive(true);
    require(audio->replay(original.get(), 480000), "Replay must schedule before destruction");
    runtime.simulateFrameWithCustomCallback(dt, [&](double) { owner->destroy(); });
}

} // namespace

int main(int argc, char **argv)
{
    test_support::headlessEnvironment();
    const std::pair<const char *, void (*)()> cases[] = {{"no_runtime", noRuntime},
                                                         {"runtime_access_lifetime", runtimeAccessLifetime},
                                                         {"growth", dispatchGrowth},
                                                         {"retirement", logicalRetirement},
                                                         {"hierarchy", hierarchy},
                                                         {"identity", identities},
                                                         {"resources", resourceRetention},
                                                         {"activation", activation},
                                                         {"callback_failure", callbackFailure},
                                                         {"audio_resources", audioRetention},
                                                         {"audio_transient_cleanup", audioTransientCleanup},
                                                         {"audio_autoplay", audioAutoplay},
                                                         {"audio_replay_completion", audioReplayCompletion},
                                                         {"audio_replay_paused", audioReplayWhilePaused},
                                                         {"audio_replay_cancellation", audioReplayCancellation}};
    int failures = 0;
    for (auto [name, run] : cases)
    {
        if (argc > 1 && std::string(argv[1]) != name)
        {
            continue;
        }
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
