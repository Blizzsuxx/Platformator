#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <json.hpp>

namespace box2d_reference
{
enum class Scene
{
    LargePyramid,
    ManyPyramids,
    Smash,
};

struct Workload
{
    Scene scene;
    std::string_view name;
    int dynamicBodies;
    int staticShapes;
};

constexpr std::array workloads{
    Workload{Scene::LargePyramid, "large_pyramid", 5050, 1},
    Workload{Scene::ManyPyramids, "many_pyramids", 22000, 20},
    Workload{Scene::Smash, "smash", 9601, 0},
};
constexpr double dt = 1.0 / 60.0;

struct Options
{
    Workload workload = workloads.front();
    int workers = 1;
    int warmupSteps = 5;
    int measureSteps = 30;
};

inline int parseInt(std::string_view value, int minimum, int maximum, std::string_view flag)
{
    int number = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), number);
    if (error != std::errc{} || end != value.data() + value.size() || number < minimum || number > maximum)
        throw std::invalid_argument("Invalid value for " + std::string(flag));
    return number;
}

inline Options parseOptions(int argc, char **argv, int maxWorkers)
{
    Options options;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view flag = argv[i];
        if (++i == argc)
            throw std::invalid_argument("Missing value for " + std::string(flag));
        const std::string_view value = argv[i];
        if (flag == "--scenario")
        {
            const auto workload = std::find_if(workloads.begin(), workloads.end(),
                                               [&](const Workload &item) { return item.name == value; });
            if (workload == workloads.end())
                throw std::invalid_argument("Unknown scenario " + std::string(value));
            options.workload = *workload;
        }
        else if (flag == "--workers")
            options.workers = parseInt(value, 1, maxWorkers, flag);
        else if (flag == "--warmup-steps")
            options.warmupSteps = parseInt(value, 0, 100000, flag);
        else if (flag == "--measure-steps")
            options.measureSteps = parseInt(value, 1, 100000, flag);
        else
            throw std::invalid_argument("Unknown option " + std::string(flag));
    }
    return options;
}

template <typename Step, typename Inspect>
void measure(const Options &options, nlohmann::json identity, Step step, Inspect inspect)
{
    using Clock = std::chrono::steady_clock;
    const auto initial = inspect();
    for (int i = 0; i < options.warmupSteps; ++i)
        step();
    const auto beforeMeasurement = inspect();
    std::vector<int64_t> samples(options.measureSteps);
    for (int64_t &sample : samples)
    {
        const auto start = Clock::now();
        step();
        sample = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
    }
    const nlohmann::json result = {{"identity", std::move(identity)},
                                  {"scenario", options.workload.name},
                                  {"workers", options.workers},
                                  {"dt_seconds", dt},
                                  {"warmup_steps", options.warmupSteps},
                                  {"measure_steps", options.measureSteps},
                                  {"initial", initial},
                                  {"before_measurement", beforeMeasurement},
                                  {"after_measurement", inspect()},
                                  {"samples_ns", samples}};
    std::cout << result.dump() << '\n' << std::flush;
    if (!std::cout)
        throw std::runtime_error("Could not write benchmark output");
}
} // namespace box2d_reference
