#include "buildconfig.h"
#include "simulationclock.h"
#include "runtime_work_probe.h"
#include "test_support.h"
#include <SDL3/SDL_events.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <platformator/camera.h>
#include <platformator/sprite.h>
#include <platformator/scriptcomponent.h>

using namespace test_support;

namespace
{
struct Trace : test_support::Behavior
{
    explicit Trace(std::string &trace) : trace(trace)
    {
    }

    void start() override
    {
        trace += 'S';
    }

    void fixedUpdate(double) override
    {
        trace += 'F';
    }

    void update(double) override
    {
        trace += 'U';
    }

    void lateUpdate(double) override
    {
        trace += 'L';
    }

    std::string &trace;
};

size_t partition(int pieces)
{
    platformator_detail::SimulationClock clock(dt, 8);
    size_t steps = 0;
    for (int i = 0; i < pieces; ++i)
    {
        steps += clock.advance(4 * dt / pieces, false, false);
    }
    require(clock.droppedSteps() == 0, "Equivalent partitions cannot drop time below the cap");
    return steps;
}
} // namespace

namespace
{
void addVisibleRenderFixture(platformator::Runtime &runtime)
{
    runtime.createMainCameraIfNoMainCameraExists();
    runtime.createGameObject()
        ->setPosition({80.0f, 70.0f})
        ->addComponent<Sprite>(runtime.loadTexture("assets/textures/black_square.png"), SDL_FLIP_NONE, 32.0f, 32.0f);
}

void requireVisibleCullingWork(const std::string &label)
{
#if PLATFORMATOR_ENABLE_WORK_PROBES
    require(platformator_test::work.renderViewPreparations == 1 &&
                platformator_test::work.renderEligibilityChecks == 1 &&
                platformator_test::work.renderVisibilityChecks == 1 &&
                platformator_test::work.renderSubmittedSprites == 1,
            label + " must render its active visible sprite through one culling view");
#else
    (void)label;
#endif
}

void renderedPausePaths()
{
#if PLATFORMATOR_ENABLE_DEBUG_TOOLS
    RuntimeOptions options;
    options.debugSettings.startPaused = true;
    options.debugSettings.showColliders = true;
    options.debugSettings.showGridCells = true;
    platformator::Runtime runtime(options);
    addVisibleRenderFixture(runtime);
    int hooks = 0;

    platformator_test::work = {};
    require(runtime.simulateAndRenderFrame(std::numeric_limits<double>::quiet_NaN()),
            "Paused plain rendering does not validate a skipped simulation dt");
    requireVisibleCullingWork("paused rendering");
    platformator_test::work = {};
    require(runtime.simulateAndRenderFrameWithCustomCallback(dt,
                                                             [&](double) {
                                                                 ++hooks;
                                                             }) &&
                hooks == 0,
            "Paused rendering skips its hook");
    requireVisibleCullingWork("paused custom rendering");

    SDL_Event advance{};
    advance.type = SDL_EVENT_KEY_DOWN;
    advance.key.key = SDLK_F6;
    require(SDL_PushEvent(&advance), "Manual advance event must queue");
    platformator_test::work = {};
    require(runtime.simulateAndRenderFrameWithCustomCallback(dt,
                                                             [&](double) {
                                                                 ++hooks;
                                                             }) &&
                hooks == 1,
            "A manual advance runs the custom hook once");
    requireVisibleCullingWork("manual-advance debug overlay rendering");
    platformator_test::work = {};
    require(runtime.simulateAndRenderFrameWithCustomCallback(dt,
                                                             [&](double) {
                                                                 ++hooks;
                                                             }) &&
                hooks == 1,
            "The manual advance request is cleared after rendering");
    requireVisibleCullingWork("paused rendering after manual advance");
#endif
}

void renderedCullingWithoutDebugTools()
{
    platformator::Runtime runtime;
    addVisibleRenderFixture(runtime);
    platformator_test::work = {};
    require(runtime.simulateAndRenderFrame(dt), "Debug-tools-off runtime must render its visible fixture");
    requireVisibleCullingWork("debug-tools-off rendering");
}
} // namespace

int main()
{
    headlessEnvironment();
    try
    {
        require(partition(1) == 4 && partition(4) == 4 && partition(40) == 4,
                "Elapsed-time partitioning must preserve fixed steps");
        platformator_detail::SimulationClock clock(dt, 8);
        require(clock.advance(20.5 * dt, false, false) == 8 && clock.droppedSteps() == 12 &&
                    std::abs(clock.remainder() - 0.5 * dt) < 1e-12,
                "Catch-up cap drops whole surplus steps and retains the fraction");
        require(clock.advance(100, true, false) == 0 && clock.remainder() == 0,
                "Pause discards elapsed wall time");
        require(clock.advance(100, true, true) == 1 && clock.remainder() == 0,
                "Manual advance performs one fixed step");
        require(clock.advance(0, false, false) == 0, "Unpause cannot replay paused wall time");
#if PLATFORMATOR_ENABLE_DEBUG_TOOLS
        renderedPausePaths();
#else
        renderedCullingWithoutDebugTools();
#endif

        std::string trace;
        platformator::Runtime runtime;
        runtime.createGameObject()
            ->addComponent<ScriptComponent>()
            ->getComponent<ScriptComponent>()
            ->addBehavior(new Trace(trace));
        runtime.simulateFrameWithCustomCallback(0.02, [&](double actual) {
            require(actual == 0.02, "Explicit host dt passes through unchanged");
            trace += 'H';
        });
        require(trace == "SHFUL", "Custom hook follows starts and precedes fixed/update/late exactly once");
        for (double invalid :
             {0., -dt, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
        {
            bool rejected = false;
            try
            {
                runtime.simulateFrame(invalid);
            }
            catch (const std::invalid_argument &)
            {
                rejected = true;
            }
            require(rejected && trace == "SHFUL", "Invalid dt rejects before any callback");
        }
        runtime.simulateFrame(dt);
        require(trace == "SHFULFUL", "Explicit steps do not use the interactive accumulator");
        require(runtime.simulateAndRenderFrame(dt), "Plain rendered stepping must run");
        require(runtime.simulateAndRenderFrameWithCustomCallback(dt,
                                                                 [&](double) {
                                                                     trace += 'H';
                                                                 }),
                "Custom rendered stepping must run");
        require(trace == "SHFULFULFULHFUL", "Both rendered paths preserve the same simulation phase order");

        auto *victim = runtime.createGameObject();
        const auto victimId = victim->getId();
        bool failed = false;
        try
        {
            runtime.simulateFrameWithCustomCallback(dt, [&](double) {
                victim->destroy();
                throw std::runtime_error("step hook failure");
            });
        }
        catch (const std::runtime_error &)
        {
            failed = true;
        }
        require(failed && runtime.getObjectById(victimId) == nullptr,
                "Hook failure propagates after pending retirement is committed");
        runtime.simulateFrame(dt);
        const auto beforeQuit = trace;

        SDL_Event quit{};
        quit.type = SDL_EVENT_QUIT;
        require(SDL_PushEvent(&quit), "Quit fixture must enter the event queue");
        require(!runtime.simulateAndRenderFrame(dt) && trace == beforeQuit,
                "Quit must stop stepping/rendered work");
        require(!runtime.simulateAndRenderFrameWithCustomCallback(dt,
                                                                  [&](double) {
                                                                      trace += 'X';
                                                                  }),
                "A stopped window cannot run custom rendered work");
        std::cout << "[PASS] simulation step and clock contracts\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
