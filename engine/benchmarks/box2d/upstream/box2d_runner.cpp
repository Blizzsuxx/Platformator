#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <box2d/box2d.h>
#include <benchmarks.h>
#include <json.hpp>

namespace
{
struct Fixture
{
    std::string_view name;
    int bodies;
    int shapes;
    b2Capacity (*capacity)();
    void (*create)(b2WorldId);
};

constexpr std::array fixtures{
    Fixture{"large_pyramid", 5051, 5051, GetLargePyramidCapacity, CreateLargePyramid},
    Fixture{"many_pyramids", 22001, 22020, GetManyPyramidsCapacity, CreateManyPyramids},
    Fixture{"smash", 9601, 9601, GetSmashCapacity, CreateSmash},
};
constexpr double dt = 1.0 / 60.0;

struct Options
{
    const Fixture *fixture = &fixtures.front();
    int workers = 1;
    int warmupSteps = 5;
    int measureSteps = 30;
};

int parseInt(std::string_view value, int minimum, int maximum, std::string_view flag)
{
    int number = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), number);
    if (error != std::errc{} || end != value.data() + value.size() || number < minimum || number > maximum)
        throw std::invalid_argument("Invalid value for " + std::string(flag));
    return number;
}

Options parseOptions(int argc, char **argv)
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
            const auto fixture = std::find_if(fixtures.begin(), fixtures.end(),
                                              [value](const Fixture &item) { return item.name == value; });
            if (fixture == fixtures.end())
                throw std::invalid_argument("Unknown scenario " + std::string(value));
            options.fixture = &*fixture;
        }
        else if (flag == "--workers")
            options.workers = parseInt(value, 1, B2_MAX_WORKERS, flag);
        else if (flag == "--warmup-steps")
            options.warmupSteps = parseInt(value, 0, 100000, flag);
        else if (flag == "--measure-steps")
            options.measureSteps = parseInt(value, 1, 100000, flag);
        else
            throw std::invalid_argument("Unknown option " + std::string(flag));
    }
    return options;
}

struct World
{
    explicit World(const b2WorldDef &def) : id(b2CreateWorld(&def))
    {
        if (!b2World_IsValid(id))
            throw std::runtime_error("Could not create Box2D world");
    }

    ~World() { b2DestroyWorld(id); }
    World(const World &) = delete;
    World &operator=(const World &) = delete;

    b2WorldId id;
};
} // namespace

int main(int argc, char **argv)
{
    try
    {
        const auto options = parseOptions(argc, argv);
        if (std::string_view(BOX2D_REFERENCE_CONFIGURATION) != "Release")
            throw std::runtime_error("Build Box2D in Release for the upstream Release scene populations");
        const auto &fixture = *options.fixture;
        auto worldDef = b2DefaultWorldDef();
        worldDef.capacity = fixture.capacity();
        worldDef.workerCount = options.workers;
        const World world(worldDef);
        if (b2World_GetWorkerCount(world.id) != options.workers)
            throw std::runtime_error("Box2D could not provide the requested worker count");
        fixture.create(world.id);
        constexpr int subSteps = 4;
        const auto inspect = [&] {
            const auto counters = b2World_GetCounters(world.id);
            if (counters.bodyCount != fixture.bodies || counters.shapeCount != fixture.shapes)
                throw std::runtime_error("Unexpected Box2D scene population");
            return nlohmann::json{{"bodies", counters.bodyCount}, {"shapes", counters.shapeCount},
                                  {"awake_dynamic_bodies", b2World_GetAwakeBodyCount(world.id)},
                                  {"box2d_contacts", counters.contactCount}};
        };
        const auto initial = inspect();
        const auto step = [&] { b2World_Step(world.id, static_cast<float>(dt), subSteps); };
        for (int i = 0; i < options.warmupSteps; ++i)
            step();
        const auto beforeMeasurement = inspect();
        using Clock = std::chrono::steady_clock;
        std::vector<int64_t> samples(options.measureSteps);
        for (int64_t &sample : samples)
        {
            const auto start = Clock::now();
            step();
            sample = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
        }
        const nlohmann::json identity = {{"engine", "Box2D v3.2.0"},
                          {"commit", BOX2D_REFERENCE_COMMIT},
                          {"configuration", BOX2D_REFERENCE_CONFIGURATION},
                          {"compiler", BOX2D_REFERENCE_COMPILER},
                          {"scheduler", "native"},
                          {"avx2_available", b2IsAVX2Available()},
                          {"double_precision", b2IsDoublePrecision()},
                          {"continuous_collision", worldDef.enableContinuous},
                          {"sub_steps", subSteps},
                          {"timed_call", "b2World_Step"}};
        const nlohmann::json result = {{"identity", identity},
                                      {"scenario", fixture.name},
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
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
