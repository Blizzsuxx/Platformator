#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace box2d_bench
{

// 10 Platformator world units = 1.0 meter so Box2D's internal meter-scale
// tolerances (b2_linearSlop, sleep/restitution thresholds) operate in their
// intended regime while simulating the exact same physical system.
constexpr float UNIT_SCALE = 0.1f;
constexpr float GRAVITY_Y = 98.1f * UNIT_SCALE; // 9.81 m/s^2

constexpr std::array<float, 16> BROAD_START_X = {
    -1400.0f, 1470.0f, -1540.0f, 1610.0f, -1680.0f, 1750.0f, -1820.0f, 1890.0f,
    -1960.0f, 2030.0f, -2100.0f, 2170.0f, -2240.0f, 2310.0f, -2380.0f, 2450.0f,
};
constexpr std::array<float, 16> BROAD_VEL_X = {
    420.0f,  -500.0f,  580.0f,  -660.0f,  740.0f,  -820.0f,  900.0f,  -980.0f,
    1060.0f, -1140.0f, 1220.0f, -1300.0f, 1380.0f, -1460.0f, 1540.0f, -1620.0f,
};
constexpr std::array<float, 8> RBC_VX = {-48.0f, 36.0f, -28.0f, 44.0f, -40.0f, 24.0f, -32.0f, 52.0f};
constexpr std::array<float, 6> RBC_VY = {0.0f, -18.0f, -12.0f, 8.0f, -6.0f, 14.0f};
constexpr std::array<float, 8> RBC_OMEGA = {-1.4f, 0.9f, -0.7f, 1.1f, -1.2f, 0.8f, -0.6f, 1.3f};

struct Options
{
    std::string scenario = "rigid_body_container";
    int count = 500;
    int warmupFrames = 120;
    int measureFrames = 600;
    int physicsFps = 120;
    int workers = 1;
    int subSteps = 4; // Box2D v3 only
    int velocityIterations = 10; // Box2D v2 only
    int positionIterations = 3;  // Box2D v2 only
    bool enableSleep = true;
};

inline Options parseOptions(int argc, char **argv)
{
    Options opts;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        auto nextVal = [&](const char *flag) -> std::string {
            if (i + 1 >= argc)
            {
                throw std::runtime_error(std::string("Missing value for ") + flag);
            }
            return argv[++i];
        };

        if (arg == "--scenario")
            opts.scenario = nextVal("--scenario");
        else if (arg == "--count")
            opts.count = std::stoi(nextVal("--count"));
        else if (arg == "--warmup-frames")
            opts.warmupFrames = std::stoi(nextVal("--warmup-frames"));
        else if (arg == "--measure-frames")
            opts.measureFrames = std::stoi(nextVal("--measure-frames"));
        else if (arg == "--physics-fps")
            opts.physicsFps = std::stoi(nextVal("--physics-fps"));
        else if (arg == "--workers")
            opts.workers = std::max(1, std::stoi(nextVal("--workers")));
        else if (arg == "--substeps")
            opts.subSteps = std::max(1, std::stoi(nextVal("--substeps")));
        else if (arg == "--velocity-iterations")
            opts.velocityIterations = std::max(1, std::stoi(nextVal("--velocity-iterations")));
        else if (arg == "--position-iterations")
            opts.positionIterations = std::max(1, std::stoi(nextVal("--position-iterations")));
        else if (arg == "--can-sleep")
        {
            const std::string v = nextVal("--can-sleep");
            opts.enableSleep = (v == "true" || v == "1");
        }
        else
        {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }
    return opts;
}

inline void printSummaryJson(const char *engineLabel, const Options &opts, const std::vector<double> &samplesMs,
                             int dynamicBodies, int staticBodies, double avgAwake, double avgContacts)
{
    std::vector<double> sorted = samplesMs;
    std::sort(sorted.begin(), sorted.end());
    const double sum = std::accumulate(samplesMs.begin(), samplesMs.end(), 0.0);
    const double meanMs = sum / static_cast<double>(samplesMs.size());
    const double medianMs = sorted[sorted.size() / 2];
    const size_t p95Idx = std::min(
        sorted.size() - 1,
        static_cast<size_t>(std::max(0.0, std::ceil(0.95 * static_cast<double>(sorted.size())) - 1.0)));
    const double p95Ms = sorted[p95Idx];

    std::printf(
        "{\"engine\":\"%s\",\"scenario\":\"%s\",\"count\":%d,\"workers\":%d,"
        "\"warmup_frames\":%d,\"measure_frames\":%d,\"physics_fps\":%d,\"unit_scale\":%.2f,"
        "\"dynamic_bodies\":%d,\"static_bodies\":%d,"
        "\"mean_ms\":%.4f,\"median_ms\":%.4f,\"p95_ms\":%.4f,"
        "\"avg_awake_bodies\":%.1f,\"avg_contacts\":%.1f}\n",
        engineLabel, opts.scenario.c_str(), opts.count, opts.workers, opts.warmupFrames, opts.measureFrames,
        opts.physicsFps, UNIT_SCALE, dynamicBodies, staticBodies, meanMs, medianMs, p95Ms, avgAwake, avgContacts);
}

} // namespace box2d_bench
