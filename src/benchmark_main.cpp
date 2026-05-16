#include <cstdio>
#include <stdexcept>
#include <string>

#include "benchmark.h"
#include "constants.h"
#include "platformator/runtime.h"

namespace
{
    constexpr const char *DEFAULT_SCENE_PATH = "assets/scenes/default.scene";

    struct BenchmarkRunnerOptions
    {
        RuntimeOptions runtimeOptions{DEFAULT_SCENE_PATH, {}, {false, false, false, false, false}};
        int warmupFrameCount = 120;
        int measureFrameCount = 600;
        double timeDelta = FRAME_TIME;
    };

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
                options.runtimeOptions.sceneFilePath = requireArgumentValue(argc, args, index, argument);
                scenePathSpecified = true;
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

            if (!argument.starts_with("--") && !scenePathSpecified)
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
        runtime.loadScene(options.runtimeOptions.sceneFilePath);

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
            "Usage: platformator_benchmark_runner [scene] [--scene path] [--warmup-frames N] [--measure-frames N] [--dt seconds]\n");
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