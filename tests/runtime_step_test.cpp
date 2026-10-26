#include "buildconfig.h"
#include "simulationclock.h"
#include "test_support.h"
#include <SDL3/SDL_events.h>
#include <cmath>
#include <iostream>
#include <limits>
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
void renderedPausePaths()
{
#if PLATFORMATOR_ENABLE_DEBUG_TOOLS
    RuntimeOptions options;
    options.debugSettings.startPaused = true;
    platformator::Runtime runtime(options);
    int hooks = 0;

    require(runtime.simulateAndRenderFrame(std::numeric_limits<double>::quiet_NaN()),
            "Paused plain rendering does not validate a skipped simulation dt");
    require(runtime.simulateAndRenderFrameWithCustomCallback(dt,
                                                             [&](double) {
                                                                 ++hooks;
                                                             }) &&
                hooks == 0,
            "Paused rendering skips its hook");

    SDL_Event advance{};
    advance.type = SDL_EVENT_KEY_DOWN;
    advance.key.key = SDLK_F6;
    require(SDL_PushEvent(&advance), "Manual advance event must queue");
    require(runtime.simulateAndRenderFrameWithCustomCallback(dt,
                                                             [&](double) {
                                                                 ++hooks;
                                                             }) &&
                hooks == 1,
            "A manual advance runs the custom hook once");
    require(runtime.simulateAndRenderFrameWithCustomCallback(dt,
                                                             [&](double) {
                                                                 ++hooks;
                                                             }) &&
                hooks == 1,
            "The manual advance request is cleared after rendering");
#endif
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
        renderedPausePaths();

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
