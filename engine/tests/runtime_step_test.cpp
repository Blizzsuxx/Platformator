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

    void afterPhysicsSync(double) override
    {
        trace += 'A';
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
    // A cached visible-rank list avoids repeating per-sprite visibility checks
    // on later frames. Both a fresh check and a cache hit must still submit the
    // active sprite through this frame's prepared view.
    require(platformator_test::work.renderViewPreparations == 1 &&
                platformator_test::work.renderEligibilityChecks == 1 &&
                platformator_test::work.renderVisibilityChecks <= 1 &&
                platformator_test::work.renderSubmittedSprites == 1,
            label + " must render its active visible sprite through one culling view (views=" +
                std::to_string(platformator_test::work.renderViewPreparations) +
                ", eligibility=" + std::to_string(platformator_test::work.renderEligibilityChecks) +
                ", visibility=" + std::to_string(platformator_test::work.renderVisibilityChecks) +
                ", submissions=" + std::to_string(platformator_test::work.renderSubmittedSprites) + ")");
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
        {
            platformator::Runtime runtime;
            runtime.createGameObject()
                ->addComponent<ScriptComponent>()
                ->getComponent<ScriptComponent>()
                ->addBehavior(new Trace(trace));
            runtime.simulateFrameWithCustomCallback(0.02, [&](double actual) {
                require(actual == 0.02, "Explicit host dt passes through unchanged");
                trace += 'H';
            });
            require(trace == "SHFULA", "Custom hook follows starts and precedes fixed/update/late/afterPhysicsSync once");
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
                require(rejected && trace == "SHFULA", "Invalid dt rejects before any callback");
            }
            runtime.simulateFrame(dt);
            require(trace == "SHFULAFULA", "Explicit steps do not use the interactive accumulator");
            require(runtime.simulateAndRenderFrame(dt), "Plain rendered stepping must run");
            require(runtime.simulateAndRenderFrameWithCustomCallback(dt,
                                                                     [&](double) {
                                                                         trace += 'H';
                                                                     }),
                    "Custom rendered stepping must run");
            require(trace == "SHFULAFULAFULAHFULA",
                    "Both rendered paths preserve the same simulation phase order");

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
            require(trace == "SHFULAFULAFULAHFULA",
                    "Step hook failure must skip afterPhysicsSync for that aborted step");
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
        }

        {
            struct ThrowingSync : test_support::Behavior
            {
                ThrowingSync(std::string &out, std::string tag, bool doThrow)
                    : out(out), tag(std::move(tag)), doThrow(doThrow)
                {
                }
                void afterPhysicsSync(double actualDt) override
                {
                    ++calls;
                    lastDt = actualDt;
                    out += tag;
                    if (doThrow)
                        throw std::runtime_error("afterPhysicsSync failure");
                }
                std::string &out;
                std::string tag;
                bool doThrow = false;
                int calls = 0;
                double lastDt = 0.0;
            };

            platformator::Runtime stepRuntime(physicsOnly());
            std::string order;
            auto *ownerA = stepRuntime.createGameObject();
            auto *b1 = new ThrowingSync(order, "1", false);
            auto *b2 = new ThrowingSync(order, "2", true);
            auto *bInactiveObj = stepRuntime.createGameObject()->setActive(false);
            auto *bInactive = new ThrowingSync(order, "I", false);
            ownerA->addComponent<ScriptComponent>()->getComponent<ScriptComponent>()->addBehavior(b1);
            bInactiveObj->addComponent<ScriptComponent>()->getComponent<ScriptComponent>()->addBehavior(bInactive);
            stepRuntime.simulateFrame(dt);
            require(order == "1" && b1->calls == 1 && bInactive->calls == 0 && b1->lastDt == dt,
                    "afterPhysicsSync must skip inactive behaviors and preserve order");

            // Adding pending b2 before step 2 starts it in Phase::Starting and dispatches its afterPhysicsSync cleanly:
            ownerA->getComponent<ScriptComponent>()->addBehavior(b2);
            bool syncThrew = false;
            try
            {
                stepRuntime.simulateFrame(dt);
            }
            catch (const std::runtime_error &)
            {
                syncThrew = true;
            }
            require(syncThrew && order == "112",
                    "Throwing in afterPhysicsSync propagates out of committed step");
            // Runtime must remain idle & steppable afterwards; disable thrower and continue cleanly.
            b2->doThrow = false;
            stepRuntime.simulateFrame(dt);
            require(order == "11212",
                    "Runtime remains valid and resumes stepping after afterPhysicsSync error");

            // Verify behavior added DURING afterPhysicsSync waits for next step startup (stays excluded in same pass).
            order.clear();
            struct AdderSync : test_support::Behavior
            {
                ScriptComponent *sc = nullptr;
                Behavior *toAdd = nullptr;
                std::string &out;
                explicit AdderSync(std::string &out) : out(out) {}
                void afterPhysicsSync(double) override
                {
                    out += 'M';
                    if (toAdd && sc)
                    {
                        sc->addBehavior(toAdd);
                        toAdd = nullptr;
                    }
                }
            };
            auto *bLatePending = new ThrowingSync(order, "N", false);
            auto *adder = new AdderSync(order);
            adder->sc = ownerA->getComponent<ScriptComponent>();
            adder->toAdd = bLatePending;
            ownerA->getComponent<ScriptComponent>()->addBehavior(adder);
            stepRuntime.simulateFrame(dt);
            require(order == "12M", "Pending behavior added during afterPhysicsSync must wait until next step start");
            stepRuntime.simulateFrame(dt);
            require(order == "12M12MN", "Newly started behavior joins afterPhysicsSync on subsequent step");

            // Verify interactive run_with_owner_service runs afterPhysicsSync before host afterSimulationStep:
            std::string interactiveOrder;
            auto *hostObj = stepRuntime.createGameObject();
            hostObj->addComponent<ScriptComponent>()->getComponent<ScriptComponent>()->addBehavior(
                new Trace(interactiveOrder));
            int hostSteps = 0;
            std::uint64_t lastStepNo = 0;
            stepRuntime.run_with_owner_service(
                [&](SDLWindow &) { return hostSteps < 1; },
                [&](std::uint64_t stepNo) {
                    ++hostSteps;
                    lastStepNo = stepNo;
                    interactiveOrder += "|post";
                });
            require(hostSteps >= 1 && lastStepNo >= 4 && interactiveOrder.find("FULA|post") != std::string::npos,
                    "afterPhysicsSync must run before host afterSimulationStep in interactive loop");
        }
        std::cout << "[PASS] simulation step and clock contracts\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
