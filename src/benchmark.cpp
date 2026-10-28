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
    "render",        "broad_phase", "narrow_phase", "resolve_collisions", "spatial_update",
    "render_culling", "render_submit", "spatial_replay", "bounds_prepare", "index_update",
    "index_maintenance", "index_query", "result_reduce"};
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
                                                                            "collision_exit_event_count",
                                                                            "spatial_proxy_count", "changed_bounds_count",
                                                                            "filter_change_count", "proxy_insert_count",
                                                                            "proxy_remove_count", "proxy_move_count",
                                                                            "envelope_reuse_count", "query_count",
                                                                            "raw_discovery_count", "exact_overlap_count",
                                                                            "pair_add_count", "pair_remove_count",
                                                                            "rebuild_call_count", "rebuild_item_count",
                                                                            "tree_height", "tree_node_count", "sprite_count",
                                                                            "visible_candidate_count", "submitted_sprite_count",
                                                                            "uncullable_sprite_count", "culling_bypass_count",
                                                                            "visibility_cache_hit_count", "visibility_cell_count"};
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
std::string metricApplicability(const char *metric, const bool renderSample, const bool rendering,
                                const std::string &broad, const std::string &culling, const bool bypass)
{
    const bool indexedPhysics = broad == "segmented" || broad == "bvh";
    const bool indexedVisibility = culling == "linear" || culling == "bvh" || culling == "segmented";
    const bool step = !renderSample;
    if (std::find(scopeNames.begin(), scopeNames.end(), metric) != scopeNames.end())
    {
        if (std::string(metric) == "render") return renderSample && rendering ? "applicable" : "inapplicable";
        if (std::string(metric) == "spatial_update") return step ? "applicable" : "inapplicable";
        if (std::string(metric) == "render_culling" || std::string(metric) == "render_submit")
            return renderSample && rendering ? "applicable" : "inapplicable";
        if (std::string(metric) == "spatial_replay") return "inapplicable";
        if (std::string(metric) == "bounds_prepare")
            return (step && indexedPhysics) || (renderSample && rendering && indexedVisibility) ? "applicable" : "inapplicable";
        if (std::string(metric) == "index_query")
            return (step && broad == "bvh") || (renderSample && rendering && (culling == "linear" || culling == "bvh"))
                       ? "applicable" : "inapplicable";
        if (std::string(metric) == "index_update" || std::string(metric) == "index_maintenance" ||
            std::string(metric) == "result_reduce")
            return (step && indexedPhysics) || (renderSample && rendering && indexedVisibility) ? "applicable" : "inapplicable";
        return step ? "applicable" : "inapplicable";
    }

    if (std::find(counterNames.begin(), counterNames.end(), metric) != counterNames.end())
    {
        const std::string name(metric);
        if (name == "occupied_cell_count") return step && broad == "segmented" ? "applicable" : "inapplicable";
        if (name == "candidate_pair_count") return step ? "applicable" : "inapplicable";
        if (std::find(counterNames.begin(), counterNames.begin() + 11, metric) != counterNames.begin() + 11)
            return step ? "applicable" : "inapplicable";
        if (name == "sprite_count") return renderSample && rendering ? "applicable" : "inapplicable";
        if (name == "submitted_sprite_count") return renderSample && rendering ? "applicable" : "inapplicable";
        if (name == "visible_candidate_count")
        {
            if (!(renderSample && rendering && indexedVisibility)) return "inapplicable";
            return bypass ? "unavailable" : "applicable";
        }
        if (name == "uncullable_sprite_count" || name == "culling_bypass_count" ||
            name == "visibility_cache_hit_count")
            return renderSample && rendering && indexedVisibility ? "applicable" : "inapplicable";
        if (name == "visibility_cell_count")
            return renderSample && rendering && culling == "segmented" ? "applicable" : "inapplicable";
        if (name == "filter_change_count" || name == "pair_add_count" || name == "pair_remove_count")
            return step && indexedPhysics ? "applicable" : "inapplicable";
        if (name == "envelope_reuse_count") return step && indexedPhysics ? "applicable" : "inapplicable";
        if (name == "rebuild_call_count" || name == "rebuild_item_count" || name == "tree_height" ||
            name == "tree_node_count")
            return (step && broad == "bvh") || (renderSample && rendering && culling == "bvh")
                       ? "applicable" : "inapplicable";
        if (name == "spatial_proxy_count" || name == "changed_bounds_count" || name == "proxy_insert_count" ||
            name == "proxy_remove_count" || name == "proxy_move_count" || name == "query_count" ||
            name == "raw_discovery_count" || name == "exact_overlap_count")
            return (step && indexedPhysics) || (renderSample && rendering && indexedVisibility)
                       ? "applicable" : "inapplicable";
    }
    return "inapplicable";
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
    renderActive = false;
    suppressCurrentRender = false;
    initializationActive = false;
    activeScopes.fill(0);
    currentFrame = {};
    currentRender = {};
    initializationStats = {};
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
    const BenchmarkFrameStats initialized = initializationStats;
    reset();
    initializationStats = initialized;
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
void BenchmarkRecorder::setInitializationActive(bool active)
{
    initializationActive = active;
}
uint64_t BenchmarkRecorder::initializationScopeNanoseconds(BenchmarkScopeId scope) const
{
    return initializationStats.scopeDurationsNanoseconds[static_cast<size_t>(scope)];
}
int64_t BenchmarkRecorder::initializationCounterValue(BenchmarkCounterId counter) const
{
    return initializationStats.counters[static_cast<size_t>(counter)];
}
void BenchmarkRecorder::beginRender()
{
    currentRender = {};
    renderActive = true;
    suppressCurrentRender = false;
}
void BenchmarkRecorder::cancelRenderSample()
{
    suppressCurrentRender = true;
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
        if (suppressCurrentRender)
        {
            renderActive = false;
            suppressCurrentRender = false;
            return;
        }
        ++totalRenders;
        if (renders.size() < renders.capacity())
        {
            currentRender.scopeDurationsNanoseconds[static_cast<size_t>(BenchmarkScopeId::Render)] = elapsed;
            renders.push_back(currentRender);
        }
        else
        {
            currentRender.scopeDurationsNanoseconds[static_cast<size_t>(BenchmarkScopeId::Render)] = elapsed;
            renders[nextRender] = currentRender;
            nextRender = (nextRender + 1) % renders.size();
        }
        renderActive = false;
    }
    else
    {
        if (frameActive)
            currentFrame.scopeDurationsNanoseconds[static_cast<size_t>(scope)] += elapsed;
        if (renderActive)
            currentRender.scopeDurationsNanoseconds[static_cast<size_t>(scope)] += elapsed;
        if (initializationActive && !frameActive && !renderActive)
            initializationStats.scopeDurationsNanoseconds[static_cast<size_t>(scope)] += elapsed;
    }
}
void BenchmarkRecorder::setCounter(BenchmarkCounterId counter, int64_t value)
{
    const size_t index = static_cast<size_t>(counter);
    const bool renderOnly = index >= static_cast<size_t>(BenchmarkCounterId::SpatialProxyCount);
    if (renderOnly && renderActive)
        currentRender.counters[index] = value;
    else if (frameActive)
        currentFrame.counters[index] = value;
    else if (initializationActive)
        initializationStats.counters[index] = value;
}
void BenchmarkRecorder::addCounter(BenchmarkCounterId counter, int64_t delta)
{
    const size_t index = static_cast<size_t>(counter);
    const bool renderOnly = index >= static_cast<size_t>(BenchmarkCounterId::SpatialProxyCount);
    if (renderOnly && renderActive)
        currentRender.counters[index] += delta;
    else if (frameActive)
        currentFrame.counters[index] += delta;
    else if (initializationActive)
        initializationStats.counters[index] += delta;
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
    const auto settings = metadata.value("spatial_settings", nlohmann::json::object());
    const auto effective = settings.value("effective", nlohmann::json::object());
    const std::string broad = effective.value("broad_phase", std::string("segmented"));
    const std::string culling = effective.value("render_culling", std::string("none"));
    const bool rendered = !renders.empty();
    nlohmann::json applicability = nlohmann::json::object();
    for (const auto *metric : scopeNames)
    {
        const bool stepApplies = !frames.empty() && metricApplicability(metric, false, rendered, broad, culling, false) == "applicable";
        const bool renderApplies = !renders.empty() && metricApplicability(metric, true, rendered, broad, culling, false) == "applicable";
        applicability[metric] = stepApplies || renderApplies ? "applicable" : "inapplicable";
    }
    for (const auto *metric : counterNames)
    {
        const bool stepApplies = !frames.empty() && metricApplicability(metric, false, rendered, broad, culling, false) == "applicable";
        const bool renderApplies = !renders.empty() && metricApplicability(metric, true, rendered, broad, culling, false) == "applicable";
        applicability[metric] = stepApplies || renderApplies ? "applicable" : "inapplicable";
    }

    const bool validation = metadata.value("validation_mode", std::string("boundary-only")) != "boundary-only";
    const std::string captureMode = metadata.value("capture_mode", validation ? std::string("validate") : std::string("performance"));
    const bool timingEligible = captureMode == "performance" && !validation;
    const std::string sourceHash = PLATFORMATOR_BENCH_SOURCE_SHA256;
    std::string caseHash = metadata.value("case_sha256", std::string());
    const bool suppliedCaseHash = caseHash.size() == 64;
    if (!suppliedCaseHash) caseHash = sourceHash;
    const auto signature = metadata.value("renderer_signature", nlohmann::json::object());
    const auto initial = metadata.value("initialization_ns", uint64_t(0));
    const int workers = static_cast<int>(metadata.value("workers", int64_t(1)));
    const auto warmup = metadata.value("warmup_steps", int64_t(0));
    const auto requested = metadata.value("measured_steps", int64_t(0));
    const auto measured = frames.size();
    nlohmann::json result = {
        {"schema_version", 4},
        {"capture_mode", captureMode},
        {"timing_eligible", timingEligible},
        {"complete_capture", totalSteps == frames.size() && totalRenders == renders.size() && totalSteps == requested},
        {"status", "complete"},
        {"run", {{"warmup_steps", warmup}, {"requested_measured_steps", requested}, {"measured_steps", measured},
                 {"seed", metadata.value("seed", int64_t(0))}, {"dt", metadata.value("dt", 1.0 / 60.0)},
                 {"workers_requested", workers}, {"workers_effective", workers},
                 {"repetitions", metadata.value("repetitions", int64_t(1))},
                 {"repetition_index", metadata.value("repetition_index", int64_t(0))}}},
        {"input", {{"domain", "engine"},
                   {"case_id", metadata.value("case_id", metadata.value("scenario", std::string("engine")))},
                   {"scenario", metadata.value("scenario", std::string())},
                   {"case_sha256", caseHash},
                   {"case_sha256_status", suppliedCaseHash ? "available" : "source-fingerprint-fallback"},
                   {"parameters", metadata.value("resolved_parameters", nlohmann::json::object())},
                   {"renderer_signature", signature},
                   {"headless", metadata.value("headless", true)},
                   {"camera_available", metadata.value("camera_available", false)},
                   {"actual_render_samples", renders.size()},
                   {"render_fallback_reason", metadata.value("render_fallback_reason", std::string())},
                   {"geometry_preparation", "included in engine step/render scopes"}}},
        {"selection", {{"requested", settings.value("requested", nlohmann::json::object())},
                       {"effective", effective}}},
        {"sample_counts", {{"warmup", warmup}, {"step", measured}, {"render", renders.size()},
                            {"measured_replay", 0}, {"actual_render", renders.size()}}},
        {"applicability", {{"sample_kind", "engine"}, {"replay_domain", nullptr},
                            {"metrics", std::move(applicability)},
                            {"units", {{"scopes", "ns"}, {"counters", "count"}}}}},
        {"boundaries", {{"complete_step", "starts through commit/destruction/transient cleanup; excludes SDL event pumping, independent render, validation oracles and recorder storage"},
                         {"physics", "transform synchronization, broad/narrow phase, support reduction, force/solve/movement and cache publication; nested in complete_step"},
                         {"cleanup", "transient audio and mutation/scene commit including destruction/free; nested in complete_step"},
                         {"scene_transition", "selected request preparation through replacement/clear publication; nested in cleanup"},
                         {"render", "actual SDL rendering after simulation/cleanup; not nested in complete_step"},
                         {"broad_phase", "legacy grid/tree reconciliation boundary; nested in physics"},
                         {"spatial_update", "selected spatial index writes and pair reconciliation; may include index maintenance/query/reduction"},
                         {"render_culling", "visibility bounds/index/query/refinement/order/cache preparation; nested in render"},
                         {"render_submit", "actual sprite submission work; nested in render"},
                         {"bounds_prepare", "selected spatial geometry and exact-bound preparation"},
                         {"index_update", "selected index mutation and segmented membership changes"},
                         {"index_maintenance", "selected dynamic-tree maintenance/rebuild; nested in spatial_update"},
                         {"index_query", "read-only selected linear/tree queries; segmented discovery is charged to index_update"},
                         {"result_reduce", "exact refinement, persistent pair/visibility update and order restoration"}}},
        {"initialization", {{"total_ns", initial},
                             {"bounds_prepare_ns", initializationScopeNanoseconds(BenchmarkScopeId::BoundsPrepare)},
                             {"index_update_ns", initializationScopeNanoseconds(BenchmarkScopeId::IndexUpdate)},
                             {"index_maintenance_ns", initializationScopeNanoseconds(BenchmarkScopeId::IndexMaintenance)},
                             {"index_query_ns", initializationScopeNanoseconds(BenchmarkScopeId::IndexQuery)},
                             {"result_reduce_ns", initializationScopeNanoseconds(BenchmarkScopeId::ResultReduce)},
                             {"initial_counters", {{"spatial_proxy_count", initializationCounterValue(BenchmarkCounterId::SpatialProxyCount)},
                                                    {"candidate_pair_count", initializationCounterValue(BenchmarkCounterId::CandidatePairCount)},
                                                    {"sprite_count", initializationCounterValue(BenchmarkCounterId::SpriteCount)},
                                                    {"visible_candidate_count", initializationCounterValue(BenchmarkCounterId::VisibleCandidateCount)}}},
                             {"status", "complete"}}},
        {"validation", {{"image", metadata.value("image_validation", nlohmann::json{{"status", "not_run"}})},
                        {"image_case_sha256", caseHash},
                        {"image_source_sha256", sourceHash},
                        {"status", validation ? "passed" : "not_run"}, {"output_hashes", nlohmann::json::array()},
                        {"physical_invariants", metadata.value("physical_invariants", nlohmann::json::object())},
                        {"policy", metadata.value("validation_mode", std::string("boundary-only"))},
                        {"timing_eligible", timingEligible}}},
        {"resources", metadata.value("resource_data", nlohmann::json{{"status", "unavailable"},
                                                                         {"reason", "resource-only runner not used"}})},
        {"build", {{"commit", PLATFORMATOR_BENCH_COMMIT}, {"dirty", bool(PLATFORMATOR_BENCH_DIRTY)},
                    {"source_sha256", sourceHash}, {"compiler", PLATFORMATOR_BENCH_COMPILER},
                    {"flags", PLATFORMATOR_BENCH_FLAGS}, {"configuration", PLATFORMATOR_BENCH_CONFIGURATION},
                    {"sanitizer", PLATFORMATOR_BENCH_SANITIZER},
                    {"work_probes", bool(PLATFORMATOR_ENABLE_WORK_PROBES)},
                    {"debug_tools", bool(PLATFORMATOR_ENABLE_DEBUG_TOOLS)},
                    {"resource_hooks", metadata.value("resource_hooks", false)}}},
        {"machine", machine()},
        {"dependencies", {{"SDL", dependencyVersion(SDL_GetVersion())},
                          {"SDL_image", dependencyVersion(IMG_Version())},
                          {"SDL_mixer", dependencyVersion(MIX_Version())},
                          {"SDL_ttf", dependencyVersion(TTF_Version())}, {"Eigen", "5.0.1"},
                          {"oneTBB", "2022.3.0"},
                          {"JSON", std::to_string(NLOHMANN_JSON_VERSION_MAJOR) + '.' +
                                       std::to_string(NLOHMANN_JSON_VERSION_MINOR) + '.' +
                                       std::to_string(NLOHMANN_JSON_VERSION_PATCH)}}}};
    return result;
}
void BenchmarkRecorder::writeOutputs(const std::string &requestedPath) const
{
    const std::filesystem::path path(requestedPath.empty() ? csvOutputPath : requestedPath);
    if (path.empty())
        throw std::invalid_argument("Benchmark CSV output path is empty");
    if (frames.empty())
        throw std::runtime_error("Benchmark produced no complete steps");
    if (totalSteps != frames.size() || totalRenders != renders.size() ||
        totalSteps != static_cast<uint64_t>(metadata.value("measured_steps", int64_t(0))))
        throw std::runtime_error("Benchmark capture is incomplete; refusing to serialize partial sample sequences");
    try
    {
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path());
        const auto spatial = metadata.value("spatial_settings", nlohmann::json::object());
        const auto effective = spatial.value("effective", nlohmann::json::object());
        const std::string broad = effective.value("broad_phase", std::string("segmented"));
        const std::string culling = effective.value("render_culling", std::string("none"));
        const bool rendering = !renders.empty();
        std::array<std::vector<double>, BENCHMARK_SCOPE_COUNT> scopeValues;
        std::array<std::vector<double>, BENCHMARK_COUNTER_COUNT> counterValues;
        const auto collect = [&](const BenchmarkFrameStats &sample, const bool isRender)
        {
            const bool bypass = sample.counters[static_cast<size_t>(BenchmarkCounterId::CullingBypassCount)] > 0;
            for (size_t metric = 0; metric < BENCHMARK_SCOPE_COUNT; ++metric)
                if (metricApplicability(scopeNames[metric], isRender, rendering, broad, culling, bypass) == "applicable")
                    scopeValues[metric].push_back(static_cast<double>(sample.scopeDurationsNanoseconds[metric]));
            for (size_t metric = 0; metric < BENCHMARK_COUNTER_COUNT; ++metric)
                if (metricApplicability(counterNames[metric], isRender, rendering, broad, culling, bypass) == "applicable")
                    counterValues[metric].push_back(static_cast<double>(sample.counters[metric]));
        };
        for (const auto &sample : frames) collect(sample, false);
        for (const auto &sample : renders) collect(sample, true);

        std::ostringstream summary;
        summary << std::setprecision(12) << "schema_version,category,name,samples,avg,median,p95,p99,min,max,unit\n";
        for (size_t scope = 0; scope < BENCHMARK_SCOPE_COUNT; ++scope)
        {
            auto values = scopeValues[scope];
            if (values.empty())
            {
                summary << "4,scope," << scopeNames[scope] << ",0,,,,,,,ms\n";
                continue;
            }
            for (auto &value : values) value /= 1e6;
            const auto stats = statistics(values);
            summary << "4,scope," << scopeNames[scope] << ',' << values.size() << ',' << stats.average << ','
                    << stats.median << ',' << stats.p95 << ',' << stats.p99 << ',' << stats.minimum << ','
                    << stats.maximum << ",ms\n";
        }
        for (size_t counter = 0; counter < BENCHMARK_COUNTER_COUNT; ++counter)
        {
            auto values = counterValues[counter];
            if (values.empty())
            {
                summary << "4,counter," << counterNames[counter] << ",0,,,,,,,count\n";
                continue;
            }
            const auto stats = statistics(values);
            summary << "4,counter," << counterNames[counter] << ',' << values.size() << ',' << stats.average << ','
                    << stats.median << ',' << stats.p95 << ',' << stats.p99 << ',' << stats.minimum << ','
                    << stats.maximum << ",count\n";
        }
        std::ostringstream raw;
        raw << "schema_version,sample_kind,index";
        for (auto scope : scopeNames)
            raw << ',' << scope << "_ns";
        for (auto counter : counterNames)
            raw << ',' << counter;
        raw << '\n';
        const auto writeSample = [&](const char *kind, size_t index, const BenchmarkFrameStats &sample, bool isRender)
        {
            raw << "4," << kind << ',' << index;
            const bool bypass = sample.counters[static_cast<size_t>(BenchmarkCounterId::CullingBypassCount)] > 0;
            for (size_t metric = 0; metric < BENCHMARK_SCOPE_COUNT; ++metric)
            {
                raw << ',';
                if (metricApplicability(scopeNames[metric], isRender, rendering, broad, culling, bypass) == "applicable")
                    raw << sample.scopeDurationsNanoseconds[metric];
            }
            for (size_t metric = 0; metric < BENCHMARK_COUNTER_COUNT; ++metric)
            {
                raw << ',';
                if (metricApplicability(counterNames[metric], isRender, rendering, broad, culling, bypass) == "applicable")
                    raw << sample.counters[metric];
            }
            raw << '\n';
        };
        for (size_t i = 0; i < frames.size(); ++i)
            writeSample("step", i, frames[(nextFrame + i) % frames.size()], false);
        for (size_t i = 0; i < renders.size(); ++i)
            writeSample("render", i, renders[(nextRender + i) % renders.size()], true);

        auto schema4Manifest = manifest();
        if (!schema4Manifest.at("complete_capture").get<bool>())
            throw std::runtime_error("Benchmark manifest is incomplete");
        auto manifestPath = path;
        manifestPath.replace_extension(".manifest.json");
        auto samplesPath = path;
        samplesPath.replace_extension(".samples.csv");
        write(path, summary.str());
        write(samplesPath, raw.str());
        write(manifestPath, schema4Manifest.dump(2) + '\n');
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
    std::fprintf(output, "[Benchmark] schema_version=4 complete_steps=%zu render_samples=%zu\n", frames.size(),
                 renders.size());
    for (size_t scope = 0; scope < BENCHMARK_SCOPE_COUNT; ++scope)
    {
        std::vector<double> values;
        values.reserve(frames.size());
        if (scope == static_cast<size_t>(BenchmarkScopeId::Render))
            for (auto value : renders)
                values.push_back(value.scopeDurationsNanoseconds[scope] / 1e6);
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
