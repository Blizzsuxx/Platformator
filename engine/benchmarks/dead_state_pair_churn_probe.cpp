#include "benchmark.h"
#include "physicsmanager.h"
#include "runtimeaccess.h"
#include "test_support.h"

#include <filesystem>
#include <iostream>
#include <oneapi/tbb/global_control.h>
#include <platformator/boxcollider.h>
#include <vector>

int main(int argc, char **argv)
{
    test_support::headlessEnvironment();
    try
    {
        test_support::require(argc == 7, "Usage: probe count workers warmup steps validate output.csv");
        const size_t count = std::stoul(argv[1]);
        const size_t workers = std::stoul(argv[2]);
        const size_t warmup = std::stoul(argv[3]);
        const size_t steps = std::stoul(argv[4]);
        const bool validate = std::stoi(argv[5]) != 0;
        const std::filesystem::path output = std::filesystem::absolute(argv[6]);
        test_support::require(count && workers && steps && !std::filesystem::exists(output), "Invalid probe arguments/output");
        for (const auto *suffix : {".samples.csv", ".manifest.json"})
        {
            auto sidecar = output;
            sidecar.replace_extension(suffix);
            test_support::require(!std::filesystem::exists(sidecar), "Probe output already exists");
        }
        std::filesystem::create_directories(output.parent_path());
        tbb::global_control limit(tbb::global_control::max_allowed_parallelism, workers);
        platformator::Runtime runtime(test_support::physicsOnly());
        std::vector<GameObject *> moved;
        moved.reserve(count);
        for (size_t i = 0; i < count; ++i)
        {
            const float x = float(i * 400);
            runtime.createGameObject()->setPosition({x, 0})->addComponent<BoxCollider>(10, 10)
                ->getComponent<BoxCollider>()->setIsTrigger(true);
            moved.push_back(runtime.createGameObject()->setPosition({x + 100, 0})->addComponent<BoxCollider>(10, 10));
        }
        const auto step = [&](size_t n) {
            runtime.simulateFrameWithCustomCallback(test_support::dt, [&](double) {
                for (size_t i = 0; i < count; ++i)
                    moved[i]->setPosition({float(i * 400) + (n % 2 == 0 ? 0.0f : 100.0f), 0});
            });
            if (validate)
            {
                const auto *physics = platformator_detail::RuntimeAccess::physicsManager();
                const size_t expected = n % 2 == 0 ? count : 0;
                test_support::require(physics->getGrid().getCandidatePairCount() == expected &&
                                          physics->getContactPairs().size() == expected,
                                      "Unexpected pair/contact count");
            }
        };
        for (size_t i = 0; i < warmup; ++i)
            step(i);
        auto &recorder = platformator_detail::BenchmarkRecorder::getInstance();
        recorder.configure(steps, {{"scenario", "dead_state_pair_churn"}, {"seed", 0}, {"workers", workers},
                                   {"dt", test_support::dt}, {"warmup_steps", warmup}, {"measured_steps", steps},
                                   {"repetitions", 1}, {"rendering", false},
                                   {"validation_mode", validate ? "per-step" : "boundary-only"},
                                   {"timing_eligible", !validate},
                                   {"resolved_parameters", {{"pair_count", count}, {"pair_spacing", 400}}}});
        for (size_t i = 0; i < steps; ++i)
        {
            step(warmup + i);
            if (validate)
            {
                const auto &frame = recorder.samples().back();
                const auto enter = frame.counters[static_cast<size_t>(platformator_detail::BenchmarkCounterId::CollisionEnterEventCount)];
                const auto exit = frame.counters[static_cast<size_t>(platformator_detail::BenchmarkCounterId::CollisionExitEventCount)];
                test_support::require(enter == ((warmup + i) % 2 == 0 ? static_cast<int64_t>(count) : 0) &&
                                          exit == ((warmup + i) % 2 == 1 ? static_cast<int64_t>(count) : 0),
                                      "Unexpected Enter/Exit count");
            }
        }
        test_support::require(recorder.samples().size() == steps, "Incomplete capture");
        const auto *physics = platformator_detail::RuntimeAccess::physicsManager();
        const size_t expectedFinal = (warmup + steps - 1) % 2 == 0 ? count : 0;
        test_support::require(physics->getGrid().getCandidatePairCount() == expectedFinal &&
                                  physics->getContactPairs().size() == expectedFinal,
                              "Unexpected final pair/contact count");
        recorder.updateMetadata({{"physical_invariants", {{"valid", true}, {"pair_count", expectedFinal}}}});
        recorder.writeOutputs(output.string());
        std::cout << "[PASS] pair churn count=" << count << " workers=" << workers << '\n';
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
