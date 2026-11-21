#include "runtimeaccess.h"
#include "buildconfig.h"
#include "sdlwindow.h"
#include "test_support.h"

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <platformator/animationclip.h>
#include <platformator/animator.h>
#include <platformator/assetreference.h>
#include <platformator/audio.h>
#include <platformator/behaviorquery.h>
#include <platformator/boxcollider.h>
#include <platformator/circlecollider.h>
#include <platformator/debugsettings.h>
#include <platformator/objectreference.h>
#include <platformator/rigidbody.h>
#include <platformator/runtimeoptions.h>
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
                ->addBehavior(new AccessOnDestroy([&, manager]
                                                  {
                require(&platformator_detail::RuntimeAccess::runtime() == &runtime &&
                            &platformator_detail::RuntimeAccess::gameManager() == manager,
                        "Runtime and manager remain accessible throughout shutdown callbacks");
                require(runtime.getRuntimeSerial() != 0,
                        "Shutdown callback can read the live identity registry");
                observed = true; }));
        }
        require(observed, "Shutdown must deliver the access observation");
        noRuntime();
    }

    void dispatchGrowth()
    {
        int survivorUpdates = 0, additions = 0;
        Runtime runtime;
        auto *driver = runtime.createGameObject()->addComponent<ScriptComponent>();
        driver->getComponent<ScriptComponent>()->addBehavior(new Action([&]
                                                                        {
        for (int i = 0; i < 100; ++i)
        {
            runtime.createGameObject()
                ->addComponent<ScriptComponent>()
                ->getComponent<ScriptComponent>()
                ->addBehavior(new Counter(additions));
        } }));
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
        driver->getComponent<ScriptComponent>()->addBehavior(new Action([&]
                                                                        { victim->destroy(); }));
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
        require(clip.getReferenceCount() == 1, "Asset reference must retain clip");
        Runtime runtime;
        auto *animator = runtime.createGameObject()->addComponent<Animator>()->getComponent<Animator>();
        animator->setCurrentClip(&clip);
        require(clip.getReferenceCount() == 2, "Animator must retain its clip");
        animator->setCurrentClip(&clip);
        require(clip.getReferenceCount() == 2, "Same clip assignment must preserve ownership");
        runtime.clearScene();
        require(clip.getReferenceCount() == 1, "Animator retirement must release its clip");
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
        runtime.simulateFrameWithCustomCallback(dt, [&](double)
                                                {
        object->setActive(false);
        object->setActive(true); });
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
        object->getComponent<ScriptComponent>()->addBehavior(new Action([&]
                                                                        {
        object->destroy();
        throw std::runtime_error("deliberate callback failure"); }));
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
        Runtime runtime;
        auto *window = platformator_detail::RuntimeAccess::window();
        require(window->enableEditorSessionControls() && window->setEditorSessionPaused(true),
                "Transient audio fixture must pause the game");
        auto *cached = runtime.loadAudio(audioPath);
        cached->addReference();
        auto *owner = runtime.createGameObject()->addComponent<Audio>(cached);
        require(owner->getComponent<Audio>()->playAndForget(), "Finite transient playback must start");
        owner->destroy();
        int steps = 0;
        for (int frame = 0; frame < 8; ++frame)
        {
            runtime.simulateAndRenderFrameWithCustomCallback(dt, [&](double)
                                                             { ++steps; });
            SDL_Delay(20);
            require(!cached->removeReferenceAndFreeIfNoReferences(),
                    "Paused transient playback must retain its asset even after its original owner is destroyed");
            cached->addReference();
        }
        require(steps == 0, "Paused transient playback must not advance simulation");
        require(window->setEditorSessionPaused(false), "Transient playback must resume with the game");
        require(waitForAudio([&]
                             {
        runtime.simulateAndRenderFrameWithCustomCallback(dt, [&](double) { ++steps; });
        // Release our probe reference, restoring it while playback still retains the asset.
        if (cached->removeReferenceAndFreeIfNoReferences())
            return true;
        cached->addReference();
        return false; }),
                "Runtime frames must release completed transient audio");
    }

    SDL_AudioDeviceID mixerDevice(MIX_Mixer *mixer)
    {
        return static_cast<SDL_AudioDeviceID>(SDL_GetNumberProperty(
            MIX_GetMixerProperties(mixer), MIX_PROP_MIXER_DEVICE_NUMBER, 0));
    }

    void checkAudioPauseControls(bool editorControls)
    {
        Runtime runtime;
        auto *window = platformator_detail::RuntimeAccess::window();
        if (editorControls)
            require(window->enableEditorSessionControls(), "Editor pause controls must initialize");
        const auto device = mixerDevice(window->getMixer());
        require(device != 0 && !SDL_AudioDevicePaused(device), "Game audio must initially run");
        auto setPaused = [&](bool value)
        {
            if (editorControls)
                require(window->setEditorSessionPaused(value), "Editor pause transition must succeed");
            else
            {
                SDL_Event event{};
                event.type = SDL_EVENT_KEY_DOWN;
                event.key.key = SDLK_F5;
                require(SDL_PushEvent(&event), "F5 pause event must queue");
                window->handleSDLEvents(dt);
            }
            require(SDL_AudioDevicePaused(device) == value, "Audio device must follow game pause");
        };
        auto *running = runtime.createGameObject()->addComponent<Audio>(runtime.loadAudio(audioPath), 1.0f, true, -1)->getComponent<Audio>();
        auto *individual = runtime.createGameObject()->addComponent<Audio>(runtime.loadAudio(audioPath), 1.0f, true, -1)->getComponent<Audio>();
        require(individual->pause(), "A track can be individually paused before game pause");
        require(waitForAudio([&]
                             { return MIX_GetTrackPlaybackPosition(running->getTrack()) > 0; }),
                "Running track must advance before pause");
        setPaused(true);
        const auto position = MIX_GetTrackPlaybackPosition(running->getTrack());
        auto *queued = runtime.createGameObject()->addComponent<Audio>(runtime.loadAudio(otherAudioPath), 1.0f, true)->getComponent<Audio>();
        auto *explicitPlay = runtime.createGameObject()->addComponent<Audio>(runtime.loadAudio(otherAudioPath))->getComponent<Audio>();
        require(explicitPlay->play(), "New playback can be requested while paused");
        for (int frame = 0; frame < 8; ++frame)
        {
            runtime.simulateAndRenderFrame(dt);
            SDL_Delay(20);
            require(MIX_GetTrackPlaybackPosition(running->getTrack()) == position,
                    "Game pause must preserve an existing sound's playback position");
            require(queued->isPlaying() && MIX_GetTrackPlaybackPosition(queued->getTrack()) == 0 &&
                        explicitPlay->isPlaying() && MIX_GetTrackPlaybackPosition(explicitPlay->getTrack()) == 0,
                    "Autoplay and explicit playback requested while paused must not consume audio");
        }
        if (editorControls)
        {
            require(window->requestEditorSessionStep(), "One simulation step must be available while paused");
            runtime.simulateAndRenderFrame(dt);
            require(SDL_AudioDevicePaused(device) && MIX_GetTrackPlaybackPosition(running->getTrack()) == position,
                    "Single-stepping must keep continuous audio suspended");
        }

        // A second mixer models the editor's independent preview output.
        std::unique_ptr<MIX_Mixer, decltype(&MIX_DestroyMixer)> preview(
            MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr), MIX_DestroyMixer);
        require(preview != nullptr && !SDL_AudioDevicePaused(mixerDevice(preview.get())),
                "Game pause must not pause a separate audio device");
        std::unique_ptr<MIX_Track, decltype(&MIX_DestroyTrack)> previewTrack(MIX_CreateTrack(preview.get()), MIX_DestroyTrack);
        require(previewTrack && MIX_SetTrackAudio(previewTrack.get(), queued->getAudioWrapper()->getAudio()) &&
                    MIX_PlayTrack(previewTrack.get(), 0),
                "Independent preview must start");
        require(waitForAudio([&]
                             { return !MIX_TrackPlaying(previewTrack.get()); }),
                "Independent preview audio must finish while the game is paused");
        require(queued->isPlaying(), "Game playback must remain frozen while the preview plays");

        setPaused(false);
        require(individual->isPaused(), "Game resume must preserve a track's individual pause state");
        require(waitForAudio([&]
                             { return !queued->isPlaying() && !explicitPlay->isPlaying(); }),
                "Sounds requested while paused must play and finish after resume");
        setPaused(true);
        setPaused(false);
        require(individual->isPaused() && running->isPlaying(), "Repeated pause cycles must preserve track states");
    }

    void audioPauseControls()
    {
        checkAudioPauseControls(true);
#if PLATFORMATOR_ENABLE_DEBUG_TOOLS
        checkAudioPauseControls(false);
#endif
    }

    void audioPausedStartup()
    {
        {
            RuntimeOptions embedded;
            embedded.editorSession = EditorSessionLaunchOptions{};
            embedded.debugSettings.startPaused = true;
            Runtime runtime(embedded);
            auto *window = platformator_detail::RuntimeAccess::window();
            require(window->isEditorSessionPaused() && SDL_AudioDevicePaused(mixerDevice(window->getMixer())),
                    "Embedded startup must suspend audio before loading scene autoplay in every build profile");
        }
        RuntimeOptions options;
        options.debugSettings.startPaused = true;
        Runtime runtime(options);
        auto *window = platformator_detail::RuntimeAccess::window();
        const auto device = mixerDevice(window->getMixer());
#if PLATFORMATOR_ENABLE_DEBUG_TOOLS
        require(SDL_AudioDevicePaused(device), "Paused startup must suspend audio before scene autoplay");
#else
        require(!SDL_AudioDevicePaused(device), "Standalone production builds must ignore debug-only pause");
#endif
        require(window->enableEditorSessionControls() && SDL_AudioDevicePaused(device),
                "Editor controls must synchronize a stored paused startup state, including without debug tools");
        auto *audio = runtime.createGameObject()->addComponent<Audio>(runtime.loadAudio(otherAudioPath), 1.0f, true)->getComponent<Audio>();
        SDL_Delay(100);
        require(audio->isPlaying() && MIX_GetTrackPlaybackPosition(audio->getTrack()) == 0,
                "Autoplay must wait at its initial position on paused startup");
        require(window->setEditorSessionPaused(false), "Paused startup must be resumable");
        require(waitForAudio([&]
                             { return !audio->isPlaying(); }),
                "Startup autoplay must finish after resume");
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
        auto *window = platformator_detail::RuntimeAccess::window();
        if (paused)
            require(window->enableEditorSessionControls() && window->setEditorSessionPaused(true),
                    "Replay fixture must pause the game");
        auto *original = runtime.loadAudio(audioPath);
        auto *next = runtime.loadAudio(otherAudioPath);
        require(original && next && original != next, "Replay requires two distinct audio assets");
        auto *owner = runtime.createGameObject()->addComponent<Audio>(original, 1.0f, true, -1.0f);
        auto *audio = owner->getComponent<Audio>();
        require(audio->replay(next, 800), "Replay must schedule a fade-out");
        require(audio->getAudioWrapper() == original, "The original sound stays bound until the fade finishes");
        int steps = 0;
        if (paused)
        {
            const auto position = MIX_GetTrackPlaybackPosition(audio->getTrack());
            const auto fade = MIX_GetTrackFadeFrames(audio->getTrack());
            for (int frame = 0; frame < 8; ++frame)
            {
                runtime.simulateAndRenderFrameWithCustomCallback(dt, [&](double)
                                                                 { ++steps; });
                SDL_Delay(20);
                require(audio->getAudioWrapper() == original && MIX_GetTrackPlaybackPosition(audio->getTrack()) == position &&
                            MIX_GetTrackFadeFrames(audio->getTrack()) == fade,
                        "Game pause must freeze the fade, playhead and pending replacement");
            }
            require(steps == 0, "Paused replay must not advance simulation");
            require(window->setEditorSessionPaused(false), "Replay must resume with the game");
        }
        require(waitForAudio([&]
                             {
        if (paused)
            runtime.simulateAndRenderFrameWithCustomCallback(dt, [&](double) { ++steps; });
        else
            runtime.simulateFrame(dt);
        return audio->getAudioWrapper() == next && audio->isPlaying(); }),
                "Replay must start the distinct target after fading");
        require(MIX_GetTrackAudio(audio->getTrack()) == next->getAudio(), "The mixer must use the replacement asset");
        require(MIX_GetTrackFadeFrames(audio->getTrack()) == 0, "The restarted track is no longer fading");
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
        runtime.simulateFrameWithCustomCallback(dt, [&](double)
                                                { owner->destroy(); });
    }

    void runtimeStartupSceneLoading()
    {
        namespace fs = std::filesystem;
        const fs::path tempDir = fs::temp_directory_path() / "platformator_startup_scene_test";
        std::error_code ec;
        fs::remove_all(tempDir, ec);
        fs::create_directories(tempDir);
        const fs::path validScene = tempDir / "startup.scene";
        {
            std::ofstream out(validScene);
            out << R"([{"id":1,"name":"AutoLoaded","tag":"","active":true,"position":{"x":0,"y":0},"rotation":0,"scale":{"x":1,"y":1},"children":[],"components":[]}])";
        }
        RuntimeOptions options = test_support::physicsOnly();
        options.sceneFilePath = validScene.string();
        {
            Runtime runtime(options);
            require(runtime.getGameObject("AutoLoaded") != nullptr,
                    "Runtime(options) must load non-empty sceneFilePath on construction");
        }
        options.sceneFilePath = (tempDir / "missing.scene").string();
        bool threw = false;
        try
        {
            Runtime runtime(options);
        }
        catch (const std::exception &)
        {
            threw = true;
        }
        fs::remove_all(tempDir, ec);
        require(threw && Runtime::tryGetCurrent() == nullptr,
                "Failed Runtime(options) startup scene load must roll back activeRuntime");
    }

    void debugSettingsAndBehaviorQuery()
    {
        DebugSettings defaults;
        require(!defaults.startPaused && !defaults.showColliders && !defaults.showCollisionPoints &&
                    !defaults.showCollisionNormals && !defaults.showGridCells && !defaults.showFps,
                "DebugSettings visual overlays must default to false");

        char arg0[] = "runner";
        char arg1[] = "--debug-draw";
        char arg2[] = "all";
        char *allDebugArgs[] = {arg0, arg1, arg2};
        const RuntimeOptions parsedAll = parseRuntimeOptions(3, allDebugArgs, "");
        require(!parsedAll.debugSettings.startPaused && parsedAll.debugSettings.showColliders &&
                    parsedAll.debugSettings.showCollisionPoints && parsedAll.debugSettings.showCollisionNormals &&
                    parsedAll.debugSettings.showGridCells && parsedAll.debugSettings.showFps,
                "--debug-draw all must enable all five visual overlays");

        char editorOverlays[] = "colliders,gridcells,normals,fps";
        char startPaused[] = "--start-paused";
        char *editorDebugArgs[] = {arg0, arg1, editorOverlays, startPaused};
        const DebugSettings editorDebug = parseRuntimeOptions(4, editorDebugArgs, "").debugSettings;
        require(editorDebug.startPaused && editorDebug.showColliders && !editorDebug.showCollisionPoints &&
                    editorDebug.showCollisionNormals && editorDebug.showGridCells && editorDebug.showFps,
                "Editor debug launch arguments must enable the selected overlays and start paused");

        Runtime runtime(test_support::physicsOnly());
        int ticks = 0;
        auto *obj = runtime.createGameObject();
        obj->addComponent<ScriptComponent>();
        auto *counter = new Counter(ticks);
        obj->getComponent<ScriptComponent>()->addBehavior(counter);
        require(::getBehavior<Counter>(obj) == counter && platformator::getBehavior<Counter>(obj) == counter &&
                    ::getBehavior<Counter>(nullptr) == nullptr,
                "Free behaviorquery helpers must resolve attached behaviors outside Behavior subclasses");
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
                                                         {"audio_pause_controls", audioPauseControls},
                                                         {"audio_paused_startup", audioPausedStartup},
                                                         {"audio_autoplay", audioAutoplay},
                                                         {"audio_replay_completion", audioReplayCompletion},
                                                         {"audio_replay_paused", audioReplayWhilePaused},
                                                         {"audio_replay_cancellation", audioReplayCancellation},
                                                         {"runtime_startup_scene", runtimeStartupSceneLoading},
                                                         {"debug_settings_and_behavior_query", debugSettingsAndBehaviorQuery}};
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
