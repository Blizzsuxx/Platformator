#include "test_support.h"
#include "benchmark.h"
#include <iostream>
#include <oneapi/tbb/global_control.h>
#include <platformator/scriptcomponent.h>
#include <platformator/boxcollider.h>
#include <platformator/camera.h>
#include "runtimeaccess.h"
#include "sdlwindow.h"

using namespace test_support;
#if PLATFORMATOR_ENABLE_BENCHMARKS
using namespace platformator_detail;
namespace
{
struct Trace
{
    bool callbacks = true, destructors = true, transition = false;
    int destroyed = 0;
};
struct RetiredWork : test_support::Behavior
{
    explicit RetiredWork(Trace &trace) : trace(trace)
    {
    }
    void onDestroy() override
    {
        auto &recorder = BenchmarkRecorder::getInstance();
        trace.callbacks &=
            recorder.scopeActive(BenchmarkScopeId::CompleteStep) && recorder.scopeActive(BenchmarkScopeId::Cleanup);
        trace.transition |= recorder.scopeActive(BenchmarkScopeId::SceneTransition);
    }
    ~RetiredWork() override
    {
        auto &recorder = BenchmarkRecorder::getInstance();
        trace.destructors &=
            recorder.scopeActive(BenchmarkScopeId::CompleteStep) && recorder.scopeActive(BenchmarkScopeId::Cleanup);
        ++trace.destroyed;
    }
    Trace &trace;
};
void spatialPhaseWallBoundaries()
{
    for (const auto execution : {BvhQueryExecution::Serial, BvhQueryExecution::Parallel})
    for (size_t workers : {size_t(1), size_t(6)})
    {
        tbb::global_control concurrency(tbb::global_control::max_allowed_parallelism, workers);
        auto options = physicsOnly(); options.spatialSettings.broadPhase = BroadPhaseMode::DynamicBvh;
        options.spatialSettings.bvhQueryExecution = execution;
        platformator::Runtime runtime(options);
        std::vector<GameObject *> boxes;
        for (int i=0; i<64; ++i)
        {
            auto *box = runtime.createGameObject()->setPosition({i*.01f,0})->addComponent<BoxCollider>(20,20);
            box->getComponent<BoxCollider>()->setIsTrigger(true); boxes.push_back(box);
        }
        auto &recorder = BenchmarkRecorder::getInstance();
        recorder.configure(1, {{"scenario", "wall-boundaries"}, {"rendering", false}, {"seed",0},
                               {"workers", workers}, {"dt",dt}, {"warmup_steps",0},
                               {"measured_steps",1}, {"repetitions",1}});
        runtime.simulateFrame(dt);
        const auto &times = recorder.samples().back().scopeDurationsNanoseconds;
        const auto ns = [&](BenchmarkScopeId id) {return times[static_cast<size_t>(id)];};
        require(ns(BenchmarkScopeId::IndexQuery) + ns(BenchmarkScopeId::ResultReduce) +
                    ns(BenchmarkScopeId::IndexUpdate) + ns(BenchmarkScopeId::IndexMaintenance) +
                    ns(BenchmarkScopeId::BoundsPrepare) <= ns(BenchmarkScopeId::SpatialUpdate),
                "Actual engine query/reduction scopes are disjoint wall intervals inside spatial update");
    }
}

void generatedEventCounters()
{
    for (int recipients = 0; recipients <= 2; ++recipients)
    {
        platformator::Runtime runtime(physicsOnly());
        auto &recorder = BenchmarkRecorder::getInstance();
        recorder.configure(8, {{"scenario", "event-contract"},
                               {"seed", 0},
                               {"workers", 1},
                               {"dt", dt},
                               {"warmup_steps", 0},
                               {"measured_steps", 8},
                               {"repetitions", 1},
                               {"rendering", false}});
        auto *a = runtime.createGameObject()->addComponent<BoxCollider>(20, 20);
        auto *b = runtime.createGameObject()->addComponent<BoxCollider>(20, 20);
        for (int endpoint = 0; endpoint < recipients; ++endpoint)
        {
            auto *owner = endpoint == 0 ? a : b;
            owner->addComponent<ScriptComponent>()->getComponent<ScriptComponent>()->addBehavior(
                new test_support::Behavior);
        }
        const auto count = [&](BenchmarkCounterId id) {
            return recorder.samples().back().counters[static_cast<size_t>(id)];
        };
        runtime.simulateFrame(dt);
        require(count(BenchmarkCounterId::CollisionEnterEventCount) == 1,
                "Generated Enter is one pair regardless of recipients");
        runtime.simulateFrame(dt);
        require(count(BenchmarkCounterId::CollisionEnterEventCount) == 0 &&
                    count(BenchmarkCounterId::CollisionExitEventCount) == 0,
                "Persistent contact does not increment transition counters");
        runtime.simulateFrameWithCustomCallback(dt, [&](double) {
            a->destroy();
            b->destroy();
        });
        require(count(BenchmarkCounterId::CollisionExitEventCount) == 1,
                "Both-retired cleanup Exit belongs to the current complete step");
        a = runtime.createGameObject()->addComponent<BoxCollider>(20, 20);
        b = runtime.createGameObject()->addComponent<BoxCollider>(20, 20);
        runtime.simulateFrame(dt);
        require(count(BenchmarkCounterId::CollisionEnterEventCount) == 1, "New listener-free lifetime is counted");
        a->destroy(); // No active recording frame.
        runtime.simulateFrame(dt);
        require(count(BenchmarkCounterId::CollisionExitEventCount) == 0,
                "Out-of-frame Exit cannot leak into next sample");
    }
}
} // namespace
#endif
int main()
{
#if PLATFORMATOR_ENABLE_BENCHMARKS
    headlessEnvironment();
    try
    {
        generatedEventCounters();
        spatialPhaseWallBoundaries();
        Trace trace;
        {
            platformator::Runtime runtime(physicsOnly());
            auto &recorder = BenchmarkRecorder::getInstance();
            recorder.configure(64, {{"scenario", "contract"},
                                    {"seed", 7},
                                    {"workers", 1},
                                    {"dt", dt},
                                    {"warmup_steps", 0},
                                    {"measured_steps", 64},
                                    {"repetitions", 1},
                                    {"rendering", false}});
            const auto capacity = recorder.sampleCapacity();
            auto *object = runtime.createGameObject()->addComponent<ScriptComponent>();
            object->getComponent<ScriptComponent>()->addBehavior(new RetiredWork(trace));
            runtime.simulateFrameWithCustomCallback(dt, [&](double) { object->destroy(); });
            object = runtime.createGameObject()->addComponent<ScriptComponent>();
            object->getComponent<ScriptComponent>()->addBehavior(new RetiredWork(trace));
            runtime.requestClearScene();
            runtime.simulateFrame(dt);
            require(trace.callbacks && trace.destructors && trace.transition && trace.destroyed == 2,
                    "Complete-step/cleanup/selected-transition scopes must include lifecycle callbacks and actual "
                    "destructors");
            for (int i = 2; i < 64; ++i)
                runtime.simulateFrame(dt);
            require(recorder.samples().size() == 64 && recorder.sampleCapacity() == capacity,
                    "Measured steps must reuse reserved sample storage");
            const auto path = std::filesystem::absolute("benchmark-contract.csv");
            recorder.writeOutputs(path.string());
            nlohmann::json manifest;
            {
                std::ifstream file(path.parent_path() / "benchmark-contract.manifest.json");
                file >> manifest;
            }
            require(manifest.at("schema_version") == 4 && manifest.at("run").at("measured_steps") == 64 &&
                        manifest.at("sample_counts").at("step") == 64 &&
                        manifest.at("sample_counts").at("render") == 0 &&
                        manifest.at("initialization").at("status") == "complete" &&
                        manifest.contains("applicability") && manifest.contains("boundaries") &&
                        manifest.contains("resources") && manifest.contains("build") &&
                        manifest.contains("machine") && manifest.contains("dependencies"),
                    "A run must record schema, boundaries, resolved parameters and build/machine/dependency identity");
            require(std::filesystem::is_regular_file(path.parent_path() / "benchmark-contract.samples.csv"),
                    "Raw step samples must accompany the summary");
            require(recorder.renderSampleCount() == 0,
                    "Headless complete-step measurements cannot manufacture render samples");
            std::filesystem::remove(path);
            std::filesystem::remove(path.parent_path() / "benchmark-contract.manifest.json");
            std::filesystem::remove(path.parent_path() / "benchmark-contract.samples.csv");
        }
        {
            platformator::Runtime runtime;
            auto *cameraObject = runtime.createGameObject();
            cameraObject->addComponent<Camera>();
            auto *camera = cameraObject->getComponent<Camera>();
            RuntimeAccess::window()->setMainCamera(camera);
            auto &recorder = BenchmarkRecorder::getInstance();
            recorder.configure(2, {{"scenario", "render-contract"},
                                   {"seed", 0},
                                   {"workers", 1},
                                   {"dt", dt},
                                   {"warmup_steps", 0},
                                   {"measured_steps", 2},
                                   {"repetitions", 1},
                                   {"rendering", true}});
            require(runtime.simulateAndRenderFrame(dt), "Rendered fixture must execute one step");
            require(
                recorder.samples().size() == 1 && recorder.renderSampleCount() == 1 &&
                    recorder.samples()[0].scopeDurationsNanoseconds[static_cast<size_t>(BenchmarkScopeId::Render)] == 0,
                "Render must be an independent sample after complete-step cleanup");
            SDL_Event event{};
            event.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&event);
            require(!runtime.simulateAndRenderFrame(dt) && recorder.samples().size() == 1 &&
                        recorder.renderSampleCount() == 1,
                    "Quit cannot count a skipped step or render sample");
            const auto incomplete = std::filesystem::absolute("incomplete-benchmark-contract.csv");
            bool rejected = false;
            try
            {
                recorder.writeOutputs(incomplete.string());
            }
            catch (const std::runtime_error &)
            {
                rejected = true;
            }
            require(rejected && !std::filesystem::exists(incomplete),
                    "Writer must refuse incomplete step/render sample sequences without a partial capture");
        }
        std::cout << "[PASS] complete-step benchmark contracts\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
#else
    std::cout << "[SKIP] benchmark instrumentation is disabled\n";
    return 77;
#endif
}
