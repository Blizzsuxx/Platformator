// Baseline-only harness: reuse the existing workload builders without changing
// engine sources or their historical timing boundaries. This targets e01526b.
#define main originalBenchmarkMain
#include "benchmark_main.cpp"
#undef main

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <oneapi/tbb/global_control.h>

#include "physicsmanager.h"
#include "runtimeaccess.h"

int main(int argc, char **argv)
{
    try
    {
        const auto options = parseBenchmarkRunnerOptions(argc, argv);
        if (options.renderFrames || options.scenario == BenchmarkScenario::None)
            throw std::runtime_error("Baseline capture requires a headless built-in scenario.");
        const char *workersText = std::getenv("PLATFORMATOR_BASELINE_WORKERS");
        const char *stepsPath = std::getenv("PLATFORMATOR_BASELINE_STEPS_CSV");
        const char *statePath = std::getenv("PLATFORMATOR_BASELINE_STATE_CSV");
        if (!workersText || !stepsPath || !statePath)
            throw std::runtime_error("Missing baseline harness environment settings.");
        const int workers = parsePositiveInteger(workersText, "PLATFORMATOR_BASELINE_WORKERS");
        const oneapi::tbb::global_control threadLimit(
            oneapi::tbb::global_control::max_allowed_parallelism, workers);
        PLATFORMATOR_BENCH_SET_CSV_OUTPUT_PATH(options.csvOutputPath);

        // Preallocate our own samples. The engine retains its original recorder
        // behavior, including any allocations it performs during measured steps.
        std::vector<int64_t> steps(options.measureFrameCount);
        platformator::Runtime runtime(options.runtimeOptions);
        AddRemoveScenarioState churn;
        BroadPhaseStressScenarioState stress;
        buildScenario(runtime, options,
            options.scenario == BenchmarkScenario::BroadPhaseStress ? &stress : nullptr,
            isAddRemoveScenario(options.scenario) ? &churn : nullptr);
        configureScenarioCamera(runtime, options,
            options.scenario == BenchmarkScenario::BroadPhaseStress ? &stress : nullptr,
            isAddRemoveScenario(options.scenario) ? &churn : nullptr);
        std::function<void(double)> callback;
        if (isAddRemoveScenario(options.scenario))
            callback = [&](double) { applyAddRemoveScenarioChurn(runtime, churn, false); };
        else if (options.scenario == BenchmarkScenario::BroadPhaseStress)
            callback = [&](double) { applyBroadPhaseStressScenarioMotion(stress); };
        const auto step = [&] {
            if (callback)
                runtime.simulateFrameWithCustomCallback(options.timeDelta, callback);
            else
                runtime.simulateFrame(options.timeDelta);
        };
        for (int i = 0; i < options.warmupFrameCount; ++i)
            step();
        PLATFORMATOR_BENCH_RESET();
        for (auto &duration : steps)
        {
            const auto start = std::chrono::steady_clock::now();
            step();
            duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - start).count();
        }

        // Writes and final-state inspection are outside all measurement windows.
        std::ofstream samples(stepsPath);
        samples.exceptions(std::ios::failbit | std::ios::badbit);
        samples << "step,host_step_ns\n";
        for (size_t i = 0; i < steps.size(); ++i)
            samples << i << ',' << steps[i] << '\n';
        samples.close();

        std::ofstream state(statePath);
        state.exceptions(std::ios::failbit | std::ios::badbit);
        state << "name,x,y,rotation,vx,vy,angular_velocity,support_count,sleeping\n";
        state << std::setprecision(9);
        for (const auto *collider : platformator_detail::RuntimeAccess::physicsManager()->getColliders())
        {
            const auto *object = collider->getGameObject();
            const auto position = object->getPosition();
            const auto *body = object->getComponent<Rigidbody>();
            const auto velocity = body ? body->getVelocity() : Eigen::Vector2f::Zero().eval();
            state << object->getName() << ',' << position.x() << ',' << position.y()
                  << ',' << object->getRotation() << ',' << velocity.x() << ',' << velocity.y()
                  << ',' << (body ? body->getAngularVelocity() : 0.0f)
                  << ',' << (body ? body->getSupportContactCount() : 0)
                  << ',' << (body && body->getIsSleeping()) << '\n';
        }
        state.close();
        // Runtime teardown writes the existing recorder's summary CSV. Teardown
        // itself is excluded from host_step, just as startup and file output are.
        return 0;
    }
    catch (const std::exception &error)
    {
        std::fprintf(stderr, "Baseline capture failed: %s\n", error.what());
        return 1;
    }
}
