#include <cstdio>
#include <stdexcept>
#include <array>
#include <string>

#include "benchmark.h"
#include "constants.h"
#include "platformator/boxcollider.h"
#include "platformator/rigidbody.h"
#include "platformator/runtime.h"

namespace
{
    constexpr const char *DEFAULT_SCENE_PATH = "assets/scenes/default.scene";

    RuntimeOptions makeDefaultBenchmarkRuntimeOptions()
    {
        RuntimeOptions runtimeOptions{DEFAULT_SCENE_PATH, {}, {false, false, false, false, false}};
        runtimeOptions.windowSettings.headless = true;
        return runtimeOptions;
    }

    enum class BenchmarkScenario
    {
        None,
        BroadPhase,
        NarrowPhase,
    };

    struct BenchmarkRunnerOptions
    {
        RuntimeOptions runtimeOptions = makeDefaultBenchmarkRuntimeOptions();
        BenchmarkScenario scenario = BenchmarkScenario::None;
        int warmupFrameCount = 120;
        int measureFrameCount = 600;
        double timeDelta = FRAME_TIME;
    };

    BenchmarkScenario parseScenario(const std::string &value)
    {
        if (value == "broad_phase" || value == "broad-phase" || value == "broadphase")
        {
            return BenchmarkScenario::BroadPhase;
        }

        if (value == "narrow_phase" || value == "narrow-phase" || value == "narrowphase")
        {
            return BenchmarkScenario::NarrowPhase;
        }

        throw std::runtime_error("Unknown benchmark scenario: " + value);
    }

    const char *scenarioName(const BenchmarkScenario scenario)
    {
        switch (scenario)
        {
        case BenchmarkScenario::BroadPhase:
            return "broad_phase";
        case BenchmarkScenario::NarrowPhase:
            return "narrow_phase";
        case BenchmarkScenario::None:
            return "scene";
        }

        return "scene";
    }

    std::string formatIndexedName(const std::string &prefix, const size_t index)
    {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%s %02zu", prefix.c_str(), index);
        return buffer;
    }

    GameObject *createBenchmarkBox(
        platformator::Runtime &runtime,
        const std::string &name,
        const float x,
        const float y,
        const float width,
        const float height,
        const BodyType bodyType,
        const bool gravity,
        const Eigen::Vector2f &velocity,
        const bool isTrigger)
    {
        GameObject *object = runtime
                                 .createGameObject()
                                 ->setName(name)
                                 ->addComponent<Rigidbody>(bodyType, gravity)
                                 ->addComponent<BoxCollider>(width, height)
                                 ->setPosition(Eigen::Vector2f(x, y));

        object->getComponent<Rigidbody>()
            ->setVelocity(velocity)
            ->setFriction(0.0f)
            ->setRestitution(0.0f);

        object->getComponent<BoxCollider>()->setIsTrigger(isTrigger);
        return object;
    }

    void buildBroadPhaseScenario(platformator::Runtime &runtime)
    {
        constexpr size_t laneCount = 128;
        constexpr float laneStartY = -1240.0f;
        constexpr float laneStepY = 80.0f;
        constexpr float laneWidth = 48.0f;
        constexpr float laneHeight = 48.0f;
        constexpr std::array<float, 16> startXPattern = {-1400.0f, 1470.0f, -1540.0f, 1610.0f, -1680.0f, 1750.0f, -1820.0f, 1890.0f, -1960.0f, 2030.0f, -2100.0f, 2170.0f, -2240.0f, 2310.0f, -2380.0f, 2450.0f};
        constexpr std::array<float, 16> velocityXPattern = {420.0f, -500.0f, 580.0f, -660.0f, 740.0f, -820.0f, 900.0f, -980.0f, 1060.0f, -1140.0f, 1220.0f, -1300.0f, 1380.0f, -1460.0f, 1540.0f, -1620.0f};

        for (size_t laneIndex = 0; laneIndex < laneCount; ++laneIndex)
        {
            const size_t patternIndex = laneIndex % startXPattern.size();
            createBenchmarkBox(
                runtime,
                formatIndexedName("Lane Mover", laneIndex),
                startXPattern[patternIndex],
                laneStartY + static_cast<float>(laneIndex) * laneStepY,
                laneWidth,
                laneHeight,
                KINEMATIC,
                false,
                Eigen::Vector2f(velocityXPattern[patternIndex], 0.0f),
                false);
        }
    }

    void buildNarrowPhaseScenario(platformator::Runtime &runtime)
    {
        constexpr std::array<float, 6> columnPositions = {-125.0f, -75.0f, -25.0f, 25.0f, 75.0f, 125.0f};
        constexpr std::array<float, 6> rowPositions = {-125.0f, -75.0f, -25.0f, 25.0f, 75.0f, 125.0f};
        constexpr std::array<float, 8> bandPositions = {-140.0f, -100.0f, -60.0f, -20.0f, 20.0f, 60.0f, 100.0f, 140.0f};
        constexpr std::array<float, 8> bandSpeeds = {20.0f, -20.0f, 24.0f, -24.0f, 20.0f, -20.0f, 24.0f, -24.0f};

        for (size_t index = 0; index < columnPositions.size(); ++index)
        {
            createBenchmarkBox(
                runtime,
                formatIndexedName("Trigger Column", index),
                columnPositions[index],
                0.0f,
                26.0f,
                360.0f,
                STATIC,
                false,
                Eigen::Vector2f::Zero(),
                true);
        }

        for (size_t index = 0; index < rowPositions.size(); ++index)
        {
            createBenchmarkBox(
                runtime,
                formatIndexedName("Trigger Row", index),
                0.0f,
                rowPositions[index],
                360.0f,
                26.0f,
                STATIC,
                false,
                Eigen::Vector2f::Zero(),
                true);
        }

        for (size_t index = 0; index < bandPositions.size(); ++index)
        {
            createBenchmarkBox(
                runtime,
                formatIndexedName("Horizontal Band", index),
                0.0f,
                bandPositions[index],
                360.0f,
                18.0f,
                KINEMATIC,
                false,
                Eigen::Vector2f(0.0f, bandSpeeds[index]),
                true);
        }

        for (size_t index = 0; index < bandPositions.size(); ++index)
        {
            createBenchmarkBox(
                runtime,
                formatIndexedName("Vertical Band", index),
                bandPositions[index],
                0.0f,
                18.0f,
                360.0f,
                KINEMATIC,
                false,
                Eigen::Vector2f(bandSpeeds[index], 0.0f),
                true);
        }
    }

    void buildScenario(platformator::Runtime &runtime, const BenchmarkScenario scenario)
    {
        switch (scenario)
        {
        case BenchmarkScenario::BroadPhase:
            buildBroadPhaseScenario(runtime);
            return;
        case BenchmarkScenario::NarrowPhase:
            buildNarrowPhaseScenario(runtime);
            return;
        case BenchmarkScenario::None:
            return;
        }
    }

    int parseNonNegativeInteger(const std::string &value, const std::string &argumentName)
    {
        const int parsedValue = std::stoi(value);
        if (parsedValue < 0)
        {
            throw std::runtime_error(argumentName + " must be greater than or equal to zero.");
        }

        return parsedValue;
    }

    int parsePositiveInteger(const std::string &value, const std::string &argumentName)
    {
        const int parsedValue = std::stoi(value);
        if (parsedValue <= 0)
        {
            throw std::runtime_error(argumentName + " must be greater than zero.");
        }

        return parsedValue;
    }

    double parsePositiveDouble(const std::string &value, const std::string &argumentName)
    {
        const double parsedValue = std::stod(value);
        if (parsedValue <= 0.0)
        {
            throw std::runtime_error(argumentName + " must be greater than zero.");
        }

        return parsedValue;
    }

    std::string requireArgumentValue(int argc, char *args[], int &index, const std::string &argumentName)
    {
        if (index + 1 >= argc)
        {
            throw std::runtime_error("Missing value for " + argumentName + ".");
        }

        index++;
        return args[index];
    }

    BenchmarkRunnerOptions parseBenchmarkRunnerOptions(int argc, char *args[])
    {
        BenchmarkRunnerOptions options;
        bool scenePathSpecified = false;

        for (int index = 1; index < argc; index++)
        {
            const std::string argument = args[index];

            if (argument == "--scene")
            {
                if (options.scenario != BenchmarkScenario::None)
                {
                    throw std::runtime_error("--scene cannot be combined with --scenario.");
                }

                options.runtimeOptions.sceneFilePath = requireArgumentValue(argc, args, index, argument);
                scenePathSpecified = true;
                continue;
            }

            if (argument == "--scenario")
            {
                if (scenePathSpecified)
                {
                    throw std::runtime_error("--scenario cannot be combined with --scene.");
                }

                options.scenario = parseScenario(requireArgumentValue(argc, args, index, argument));
                continue;
            }

            if (argument == "--warmup-frames")
            {
                options.warmupFrameCount = parseNonNegativeInteger(requireArgumentValue(argc, args, index, argument), argument);
                continue;
            }

            if (argument == "--measure-frames")
            {
                options.measureFrameCount = parsePositiveInteger(requireArgumentValue(argc, args, index, argument), argument);
                continue;
            }

            if (argument == "--dt")
            {
                options.timeDelta = parsePositiveDouble(requireArgumentValue(argc, args, index, argument), argument);
                continue;
            }

            if (!argument.starts_with("--") && !scenePathSpecified && options.scenario == BenchmarkScenario::None)
            {
                options.runtimeOptions.sceneFilePath = argument;
                scenePathSpecified = true;
                continue;
            }

            throw std::runtime_error("Unknown benchmark argument: " + argument);
        }

        return options;
    }

    void runBenchmark(const BenchmarkRunnerOptions &options)
    {
        platformator::Runtime runtime(options.runtimeOptions);

        if (options.scenario == BenchmarkScenario::None)
        {
            runtime.loadScene(options.runtimeOptions.sceneFilePath);
        }
        else
        {
            std::fprintf(stdout, "[Benchmark] scenario=%s\n", scenarioName(options.scenario));
            buildScenario(runtime, options.scenario);
        }

        for (int frameIndex = 0; frameIndex < options.warmupFrameCount; frameIndex++)
        {
            runtime.simulateFrame(options.timeDelta);
        }

        PLATFORMATOR_BENCH_RESET();

        for (int frameIndex = 0; frameIndex < options.measureFrameCount; frameIndex++)
        {
            runtime.simulateFrame(options.timeDelta);
        }
    }

    void printUsage(FILE *output)
    {
        std::fprintf(
            output,
            "Usage: platformator_benchmark_runner [scene] [--scene path | --scenario broad_phase|narrow_phase] [--warmup-frames N] [--measure-frames N] [--dt seconds]\n");
    }
}

int main(int argc, char *args[])
{
    try
    {
        runBenchmark(parseBenchmarkRunnerOptions(argc, args));
        return 0;
    }
    catch (const std::exception &exception)
    {
        std::fprintf(stderr, "%s\n", exception.what());
        printUsage(stderr);
        return 1;
    }
}