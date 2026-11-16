// Diagnostic for current pause behavior. Does not change the runtime.
#include "buildconfig.h"
#include "runtimeaccess.h"
#include "sdlwindow.h"
#include "test_support.h"
#include <platformator/animationclip.h>
#include <platformator/animator.h>
#include <platformator/audio.h>
#include <platformator/rigidbody.h>
#include <platformator/scriptcomponent.h>
#include <iostream>

using test_support::require;
using test_support::dt;

struct Counters
{
    int starts = 0, fixed = 0, updates = 0, late = 0;
};

struct Counter : test_support::Behavior
{
    explicit Counter(Counters &counts) : counts(counts) {}
    void start() override { ++counts.starts; }
    void fixedUpdate(double) override { ++counts.fixed; }
    void update(double) override { ++counts.updates; }
    void lateUpdate(double) override { ++counts.late; }
    Counters &counts;
};

void investigate(bool editorControls)
{
    RuntimeOptions options;
    options.assetRoot = std::filesystem::absolute("assets").string();
    options.windowSettings.hidden = true;
    options.windowSettings.width = options.windowSettings.height = 64;
    platformator::Runtime runtime(options);
    auto *window = platformator_detail::RuntimeAccess::window();
    Counters counts;
    auto *object = runtime.createGameObject();
    object->addComponent<ScriptComponent>()->getComponent<ScriptComponent>()->addBehavior(new Counter(counts));
    auto *body = object->addComponent<Rigidbody>(DYNAMIC, false)->getComponent<Rigidbody>();
    body->setVelocity({30, 0});
    runtime.simulateFrame(dt);
    auto *clip = runtime.loadAnimationClip("assets/animations/missing.animset");
    clip->frames.push_back(clip->frames.front());
    for (auto &frame : clip->frames)
        frame.duration = static_cast<float>(dt);
    auto *animator = object->addComponent<Animator>()->getComponent<Animator>();
    require(animator->play(clip), "Animation must start");
    animator->setCurrentFrameIndex(0);

    if (editorControls)
    {
        window->enableEditorSessionControls();
        require(window->setEditorSessionPaused(true), "Editor pause must be accepted");
    }
    else
    {
        SDL_Event pause{};
        pause.type = SDL_EVENT_KEY_DOWN;
        pause.key.key = SDLK_F5;
        require(SDL_PushEvent(&pause), "F5 event must queue");
        window->handleSDLEvents(dt);
    }
    require(!window->shouldSimulateFrame(), "Simulation must be paused");
    const auto position = object->getPosition().eval();

    auto *audio = runtime.createGameObject()->addComponent<Audio>(runtime.loadAudio("assets/audio/missing.wav"))
                      ->getComponent<Audio>();
    require(audio->play(), "Audio starts even while game is paused");
    require(!audio->isPaused(), "Game pause does not pause the audio track");

    int events = 0, captures = 0, services = 0, hooks = 0;
    window->addSdlListener([&](SDL_Event event, double) {
        if (event.type == SDL_EVENT_USER)
            ++events;
    });
    window->setEditorSessionFrameCapture([&](SDL_Renderer *, int, int) { ++captures; });
    SDL_Event user{};
    user.type = SDL_EVENT_USER;
    require(SDL_PushEvent(&user), "Input/event fixture must queue");
    runtime.run_with_owner_service([&] { return ++services <= 6; });
    for (int i = 0; i < 6; ++i)
    {
        runtime.simulateAndRenderFrameWithCustomCallback(dt, [&](double) { ++hooks; });
        SDL_Delay(25);
    }
    require(counts.starts == 1 && counts.fixed == 1 && counts.updates == 1 && counts.late == 1 && hooks == 0,
            "All simulation callbacks must stay frozen");
    require(object->getPosition() == position, "Physics position must stay frozen");
    require(animator->getCurrentFrameIndex() == 0, "Animator must stay frozen");
    require(!audio->isPlaying() && !audio->isPaused(), "Audio must finish despite game pause");
    require(events == 1 && captures == 12 && services == 7, "Events, rendering and owner services continue");
    std::cout << (editorControls ? "editor pause" : "F5 pause")
              << ": physics/scripts/animation frozen; audio started and finished; events=" << events
              << ", renders=" << captures << ", service calls=" << services << '\n';

    runtime.simulateFrame(0.012);
    require(counts.fixed == 2 && counts.updates == 2 && counts.late == 2 && object->getPosition() != position &&
                animator->getCurrentFrameIndex() == 1 && !window->shouldSimulateFrame(),
            "Explicit simulation bypasses pause for callbacks, physics and animation");
    std::cout << "explicit simulateFrame: advances scripts, physics and animation while pause remains set\n";
    window->setEditorSessionFrameCapture({});
}

int main()
{
    test_support::headlessEnvironment(); // Dummy backends, with a live audio mixer and window.
    try
    {
        investigate(true);
#if PLATFORMATOR_ENABLE_DEBUG_TOOLS
        investigate(false);
#endif
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
