#include "benchmark.h"
#if PLATFORMATOR_ENABLE_BENCHMARKS
#include "benchmarkbuildconfig.h"
#include <SDL3/SDL_version.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>
#include <oneapi/tbb/global_control.h>
#if defined(__unix__)
#include <sys/utsname.h>
#endif

namespace
{
using namespace platformator_detail;
constexpr std::array<const char *, BENCHMARK_SCOPE_COUNT> scopeNames = {
    "complete_step", "physics",     "cleanup",      "scene_transition",
    "render",        "broad_phase", "narrow_phase", "resolve_collisions"};
constexpr std::array<const char *, BENCHMARK_COUNTER_COUNT> counterNames = {"object_count",
                                                                            "occupied_cell_count",
                                                                            "candidate_pair_count",
                                                                            "pending_narrow_phase_pair_count",
                                                                            "active_collision_count",
                                                                            "awake_dynamic_body_count",
                                                                            "queued_add_count",
                                                                            "queued_sync_count",
                                                                            "queued_remove_count",
                                                                            "collision_enter_event_count",
                                                                            "collision_stay_event_count",
                                                                            "collision_exit_event_count"};
std::string dependencyVersion(int version)
{
    return std::to_string(SDL_VERSIONNUM_MAJOR(version)) + '.' + std::to_string(SDL_VERSIONNUM_MINOR(version)) + '.' +
           std::to_string(SDL_VERSIONNUM_MICRO(version));
}
struct Statistics
{
    double average = 0, median = 0, p95 = 0, p99 = 0, minimum = 0, maximum = 0;
};
Statistics statistics(std::vector<double> &values)
{
    if (values.empty())
        return {};
    long double total = 0;
    for (auto value : values)
        total += value;
    std::sort(values.begin(), values.end());
    const auto percentile = [&](double p) { return values[static_cast<size_t>(std::ceil(p * values.size())) - 1]; };
    return {static_cast<double>(total / values.size()),
            values[values.size() / 2],
            percentile(.95),
            percentile(.99),
            values.front(),
            values.back()};
}
void write(const std::filesystem::path &path, const std::string &contents)
{
    std::ofstream file;
    file.exceptions(std::ios::badbit | std::ios::failbit);
    file.open(path, std::ios::binary);
    file << contents;
    file.close();
}
nlohmann::json machine()
{
    nlohmann::json result = {{"logical_cpus", std::thread::hardware_concurrency()}};
#if defined(__unix__)
    utsname system{};
    if (uname(&system) == 0)
        result.update({{"os", system.sysname}, {"kernel", system.release}, {"architecture", system.machine}});
#endif
    std::ifstream cpu("/proc/cpuinfo");
    for (std::string line; std::getline(cpu, line);)
        if (line.starts_with("model name"))
        {
            result["cpu"] = line.substr(line.find(':') + 2);
            break;
        }
    return result;
}
} // namespace

namespace platformator_detail
{
BenchmarkRecorder &BenchmarkRecorder::getInstance()
{
    static BenchmarkRecorder recorder;
    return recorder;
}
BenchmarkRecorder::BenchmarkRecorder()
{
    reset();
}
void BenchmarkRecorder::reset()
{
    frameActive = false;
    activeScopes.fill(0);
    currentFrame = {};
    frames.clear();
    renders.clear();
    frames.reserve(4096);
    renders.reserve(4096);
    nextFrame = nextRender = 0;
    totalSteps = 0;
    totalRenders = 0;
    metadata = {{"scenario", "host"},
                {"seed", 0},
                {"workers", tbb::global_control::active_value(tbb::global_control::max_allowed_parallelism)},
                {"dt", nullptr},
                {"warmup_steps", 0},
                {"measured_steps", 0},
                {"repetitions", 1},
                {"rendering", false}};
}
void BenchmarkRecorder::configure(size_t expectedSamples, nlohmann::json run)
{
    if (!expectedSamples || !run.contains("scenario") || !run.contains("seed") || !run.contains("workers") ||
        !run.contains("dt") || !run.contains("warmup_steps") || !run.contains("measured_steps") ||
        !run.contains("repetitions") || !run.contains("rendering"))
        throw std::invalid_argument("Incomplete benchmark run configuration");
    const double dt = run.at("dt").get<double>();
    if (!std::isfinite(dt) || dt <= 0 || run.at("workers").get<int64_t>() <= 0 ||
        run.at("repetitions").get<int64_t>() <= 0)
        throw std::invalid_argument("Invalid benchmark dt/workers/repetitions");
    reset();
    frames.reserve(expectedSamples);
    renders.reserve(expectedSamples);
    metadata = std::move(run);
}
void BenchmarkRecorder::beginFrame()
{
    assert(!frameActive);
    currentFrame = {};
    frameStartTime = std::chrono::steady_clock::now();
    frameActive = true;
}
void BenchmarkRecorder::endFrame()
{
    assert(frameActive);
    currentFrame.scopeDurationsNanoseconds[static_cast<size_t>(BenchmarkScopeId::CompleteStep)] =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - frameStartTime).count();
    if (frames.size() < frames.capacity())
        frames.push_back(currentFrame);
    else
    {
        frames[nextFrame] = currentFrame;
        nextFrame = (nextFrame + 1) % frames.size();
    }
    ++totalSteps;
    frameActive = false;
}
void BenchmarkRecorder::enterScope(BenchmarkScopeId scope)
{
    ++activeScopes[static_cast<size_t>(scope)];
}
void BenchmarkRecorder::leaveScope(BenchmarkScopeId scope, uint64_t elapsed)
{
    auto &active = activeScopes[static_cast<size_t>(scope)];
    assert(active);
    --active;
    addScopeDuration(scope, elapsed);
}
bool BenchmarkRecorder::scopeActive(BenchmarkScopeId scope) const
{
    return scope == BenchmarkScopeId::CompleteStep ? frameActive : activeScopes[static_cast<size_t>(scope)] != 0;
}
void BenchmarkRecorder::addScopeDuration(BenchmarkScopeId scope, uint64_t elapsed)
{
    if (scope == BenchmarkScopeId::Render)
    {
        ++totalRenders;
        if (renders.size() < renders.capacity())
            renders.push_back(elapsed);
        else
        {
            renders[nextRender] = elapsed;
            nextRender = (nextRender + 1) % renders.size();
        }
    }
    else if (frameActive)
        currentFrame.scopeDurationsNanoseconds[static_cast<size_t>(scope)] += elapsed;
}
void BenchmarkRecorder::setCounter(BenchmarkCounterId counter, int64_t value)
{
    if (frameActive)
        currentFrame.counters[static_cast<size_t>(counter)] = value;
}
void BenchmarkRecorder::addCounter(BenchmarkCounterId counter, int64_t delta)
{
    if (frameActive)
        currentFrame.counters[static_cast<size_t>(counter)] += delta;
}
void BenchmarkRecorder::setCsvOutputPath(const std::string &path)
{
    csvOutputPath = path;
}
void BenchmarkRecorder::clearCsvOutputPath()
{
    csvOutputPath.clear();
}
void BenchmarkRecorder::updateMetadata(const nlohmann::json &fields)
{
    metadata.update(fields);
}
nlohmann::json BenchmarkRecorder::manifest() const
{
    auto result = metadata;
    result["schema_version"] = 2;
    result["requested_measured_steps"] = metadata.at("measured_steps");
    result["measured_steps"] = frames.size();
    result["complete_capture"] = totalSteps == frames.size() && totalRenders == renders.size();
    result["total_steps"] = totalSteps;
    result["render_samples"] = renders.size();
    result["total_render_samples"] = totalRenders;
    result["sample_capacity"] = frames.capacity();
    result["build"] = {{"commit", PLATFORMATOR_BENCH_COMMIT},
                       {"dirty", bool(PLATFORMATOR_BENCH_DIRTY)},
                       {"source_sha256", PLATFORMATOR_BENCH_SOURCE_SHA256},
                       {"compiler", PLATFORMATOR_BENCH_COMPILER},
                       {"flags", PLATFORMATOR_BENCH_FLAGS},
                       {"configuration", PLATFORMATOR_BENCH_CONFIGURATION},
                       {"sanitizer", PLATFORMATOR_BENCH_SANITIZER},
                       {"work_probes", bool(PLATFORMATOR_ENABLE_WORK_PROBES)},
                       {"debug_tools", bool(PLATFORMATOR_ENABLE_DEBUG_TOOLS)}};
    result["machine"] = machine();
    result["dependencies"] = {{"SDL", dependencyVersion(SDL_GetVersion())},
                              {"SDL_image", dependencyVersion(IMG_Version())},
                              {"SDL_mixer", dependencyVersion(MIX_Version())},
                              {"SDL_ttf", dependencyVersion(TTF_Version())},
                              {"Eigen", "5.0.1"},
                              {"oneTBB", "2022.3.0"},
                              {"JSON", std::to_string(NLOHMANN_JSON_VERSION_MAJOR) + '.' +
                                           std::to_string(NLOHMANN_JSON_VERSION_MINOR) + '.' +
                                           std::to_string(NLOHMANN_JSON_VERSION_PATCH)}};
    result["boundaries"] = {
        {"complete_step", "starts/hook/fixed/synchronization/physics/events/update/animation/late/commit/destructors/"
                          "transient cleanup; excludes event pumping, render and recorder storage"},
        {"physics", "transform synchronization, broad/narrow phase, support reduction, forces, solve, movement and "
                    "cache publication; nested in complete_step"},
        {"cleanup", "transient audio and mutation/scene commit including destruction/free; nested in complete_step"},
        {"scene_transition", "selected request preparation through replacement/clear publication; nested in cleanup"},
        {"render", "independent render calls after cleanup; not nested in complete_step"},
        {"broad_phase", "grid/cell/filter/witness reconciliation; nested in physics"},
        {"narrow_phase", "changed geometry, serial support reduction and subscribed events; nested in physics"},
        {"resolve_collisions", "contact solving and sleep reduction; nested in physics"},
        {"legacy_frame", "historical partial metric; absent from schema v2; not equivalent to complete_step"}};
    if (!result.contains("physical_invariants"))
        result["physical_invariants"] = {{"validation", "not_run"}};
    return result;
}
void BenchmarkRecorder::writeOutputs(const std::string &requestedPath) const
{
    const std::filesystem::path path(requestedPath.empty() ? csvOutputPath : requestedPath);
    if (path.empty())
        throw std::invalid_argument("Benchmark CSV output path is empty");
    if (frames.empty())
        throw std::runtime_error("Benchmark produced no complete steps");
    try
    {
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path());
        std::ostringstream summary;
        summary << std::setprecision(12) << "schema_version,category,name,samples,avg,median,p95,p99,min,max,unit\n";
        for (size_t scope = 0; scope < BENCHMARK_SCOPE_COUNT; ++scope)
        {
            std::vector<double> values;
            values.reserve(frames.size());
            if (scope == static_cast<size_t>(BenchmarkScopeId::Render))
                for (auto value : renders)
                    values.push_back(value / 1e6);
            else
                for (const auto &frame : frames)
                    values.push_back(frame.scopeDurationsNanoseconds[scope] / 1e6);
            const auto stats = statistics(values);
            summary << "2,scope," << scopeNames[scope] << ',' << values.size() << ',' << stats.average << ','
                    << stats.median << ',' << stats.p95 << ',' << stats.p99 << ',' << stats.minimum << ','
                    << stats.maximum << ",ms\n";
        }
        for (size_t counter = 0; counter < BENCHMARK_COUNTER_COUNT; ++counter)
        {
            std::vector<double> values;
            values.reserve(frames.size());
            for (const auto &frame : frames)
                values.push_back(frame.counters[counter]);
            const auto stats = statistics(values);
            summary << "2,counter," << counterNames[counter] << ',' << values.size() << ',' << stats.average << ",,,,"
                    << stats.minimum << ',' << stats.maximum << ",count\n";
        }
        std::ostringstream raw;
        raw << "schema_version,sample_kind,index";
        for (auto scope : scopeNames)
            raw << ',' << scope << "_ns";
        for (auto counter : counterNames)
            raw << ',' << counter;
        raw << '\n';
        for (size_t i = 0; i < frames.size(); ++i)
        {
            const auto &frame = frames[(nextFrame + i) % frames.size()];
            raw << "2,step," << i;
            for (auto value : frame.scopeDurationsNanoseconds)
                raw << ',' << value;
            for (auto value : frame.counters)
                raw << ',' << value;
            raw << '\n';
        }
        for (size_t i = 0; i < renders.size(); ++i)
        {
            raw << "2,render," << i;
            for (size_t scope = 0; scope < BENCHMARK_SCOPE_COUNT; ++scope)
                raw << ','
                    << (scope == static_cast<size_t>(BenchmarkScopeId::Render)
                            ? renders[(nextRender + i) % renders.size()]
                            : 0);
            for (size_t counter = 0; counter < BENCHMARK_COUNTER_COUNT; ++counter)
                raw << ",0";
            raw << '\n';
        }
        auto samplesPath = path;
        samplesPath.replace_extension(".samples.csv");
        auto manifestPath = path;
        manifestPath.replace_extension(".manifest.json");
        write(path, summary.str());
        write(samplesPath, raw.str());
        write(manifestPath, manifest().dump(2) + '\n');
    }
    catch (const std::exception &error)
    {
        throw std::runtime_error("Benchmark output '" + path.string() + "': " + error.what());
    }
}
void BenchmarkRecorder::printSummary(FILE *output) const
{
    if (frames.empty())
        return;
    std::fprintf(output, "[Benchmark] schema_version=2 complete_steps=%zu render_samples=%zu\n", frames.size(),
                 renders.size());
    for (size_t scope = 0; scope < BENCHMARK_SCOPE_COUNT; ++scope)
    {
        std::vector<double> values;
        values.reserve(frames.size());
        if (scope == static_cast<size_t>(BenchmarkScopeId::Render))
            for (auto value : renders)
                values.push_back(value / 1e6);
        else
            for (const auto &frame : frames)
                values.push_back(frame.scopeDurationsNanoseconds[scope] / 1e6);
        const auto stats = statistics(values);
        std::fprintf(output, "[Benchmark][Scope] %s avg_ms=%.6f median_ms=%.6f p95_ms=%.6f samples=%zu\n",
                     scopeNames[scope], stats.average, stats.median, stats.p95,
                     scope == static_cast<size_t>(BenchmarkScopeId::Render) ? renders.size() : frames.size());
    }
}
BenchmarkScopeTimer::BenchmarkScopeTimer(BenchmarkScopeId scope, bool enabled) : scope(scope), enabled(enabled)
{
    if (enabled)
    {
        started = std::chrono::steady_clock::now();
        BenchmarkRecorder::getInstance().enterScope(scope);
    }
}
BenchmarkScopeTimer::~BenchmarkScopeTimer()
{
    if (enabled)
        BenchmarkRecorder::getInstance().leaveScope(
            scope,
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started).count());
}
} // namespace platformator_detail
#endif
