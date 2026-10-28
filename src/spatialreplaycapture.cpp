#include "benchmark.h"

#if PLATFORMATOR_ENABLE_BENCHMARKS
#include "benchmarkbuildconfig.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string_view>
#include <thread>
#include <cstdint>
#include <vector>

#include <SDL3/SDL_version.h>
#include <oneapi/tbb/global_control.h>

namespace platformator_detail
{
namespace
{
constexpr std::array<const char *, 17> scopes = {
    "complete_step", "physics", "cleanup", "scene_transition", "render", "broad_phase", "narrow_phase",
    "resolve_collisions", "spatial_update", "render_culling", "render_submit", "spatial_replay",
    "bounds_prepare", "index_update", "index_maintenance", "index_query", "result_reduce"};
constexpr std::array<const char *, 34> counters = {
    "object_count", "occupied_cell_count", "candidate_pair_count", "pending_narrow_phase_pair_count",
    "active_collision_count", "awake_dynamic_body_count", "queued_add_count", "queued_sync_count",
    "queued_remove_count", "collision_enter_event_count", "collision_exit_event_count", "spatial_proxy_count",
    "changed_bounds_count", "filter_change_count", "proxy_insert_count", "proxy_remove_count", "proxy_move_count",
    "envelope_reuse_count", "query_count", "raw_discovery_count", "exact_overlap_count", "pair_add_count",
    "pair_remove_count", "rebuild_call_count", "rebuild_item_count", "tree_height", "tree_node_count",
    "sprite_count", "visible_candidate_count", "submitted_sprite_count", "uncullable_sprite_count",
    "culling_bypass_count", "visibility_cache_hit_count", "visibility_cell_count"};

struct Stats { double avg = 0, median = 0, p95 = 0, p99 = 0, minimum = 0, maximum = 0; };
Stats summarize(std::vector<double> values)
{
    if (values.empty()) return {};
    long double total = 0;
    for (const auto value : values) total += value;
    std::sort(values.begin(), values.end());
    const auto percentile = [&](double p) { return values[static_cast<size_t>(std::ceil(p * values.size())) - 1]; };
    return {static_cast<double>(total / values.size()), values[values.size() / 2], percentile(.95), percentile(.99),
            values.front(), values.back()};
}

std::string broadName(BroadPhaseMode mode) { return mode == BroadPhaseMode::DynamicBvh ? "bvh" : "segmented"; }
std::string renderName(RenderCullingMode mode)
{
    switch (mode)
    {
    case RenderCullingMode::None: return "none";
    case RenderCullingMode::Linear: return "linear";
    case RenderCullingMode::DynamicBvh: return "bvh";
    case RenderCullingMode::Segmented: return "segmented";
    }
    return "unknown";
}

bool scopeApplies(const char *name, const SpatialTrace &trace, const SpatialSettings &settings)
{
    const bool collision = trace.header.domain == SpatialTraceDomain::Collision;
    const bool indexed = collision || settings.renderCulling != RenderCullingMode::None;
    const std::string_view metric(name);
    if (metric == "spatial_replay") return true;
    if (metric == "bounds_prepare") return !collision && indexed;
    if (metric == "index_update" || metric == "index_maintenance" || metric == "result_reduce") return indexed;
    if (metric == "index_query")
        return collision ? settings.broadPhase == BroadPhaseMode::DynamicBvh
                         : settings.renderCulling == RenderCullingMode::Linear ||
                               settings.renderCulling == RenderCullingMode::DynamicBvh;
    return false;
}

std::optional<uint64_t> scopeValue(const char *name, const SpatialReplayCounters &value,
                                   const SpatialReplaySample *sample, const SpatialTrace &trace,
                                   const SpatialSettings &settings)
{
    if (!scopeApplies(name, trace, settings)) return std::nullopt;
    const std::string_view metric(name);
    if (metric == "spatial_replay") return sample ? sample->durationNanoseconds : 0;
    if (metric == "bounds_prepare") return value.boundsPrepareNanoseconds;
    if (metric == "index_update") return value.indexUpdateNanoseconds;
    if (metric == "index_maintenance") return value.indexMaintenanceNanoseconds;
    if (metric == "index_query") return value.indexQueryNanoseconds;
    if (metric == "result_reduce") return value.resultReduceNanoseconds;
    return 0;
}

std::optional<uint64_t> counterValue(const char *name, const SpatialReplayCounters &value,
                                     const SpatialTrace &trace, const SpatialSettings &settings)
{
    const bool collision = trace.header.domain == SpatialTraceDomain::Collision;
    const bool visibility = !collision;
    const bool indexed = collision || settings.renderCulling != RenderCullingMode::None;
    const bool bvh = collision ? settings.broadPhase == BroadPhaseMode::DynamicBvh
                               : settings.renderCulling == RenderCullingMode::DynamicBvh;
    const std::string_view metric(name);
    if (metric == "candidate_pair_count") return collision ? std::optional<uint64_t>(value.candidatePairCount) : std::nullopt;
    if (metric == "spatial_proxy_count") return indexed ? std::optional<uint64_t>(value.proxyCount) : std::nullopt;
    if (metric == "changed_bounds_count") return indexed ? std::optional<uint64_t>(value.changedBounds) : std::nullopt;
    if (metric == "filter_change_count") return collision ? std::optional<uint64_t>(value.filterChanges) : std::nullopt;
    if (metric == "proxy_insert_count") return indexed ? std::optional<uint64_t>(value.proxyInserts) : std::nullopt;
    if (metric == "proxy_remove_count") return indexed ? std::optional<uint64_t>(value.proxyRemoves) : std::nullopt;
    if (metric == "proxy_move_count") return indexed ? std::optional<uint64_t>(value.proxyMoves) : std::nullopt;
    if (metric == "envelope_reuse_count") return collision ? std::optional<uint64_t>(value.envelopeReuse) : std::nullopt;
    if (metric == "query_count") return indexed ? std::optional<uint64_t>(value.queries) : std::nullopt;
    if (metric == "raw_discovery_count") return indexed ? std::optional<uint64_t>(value.rawDiscoveries) : std::nullopt;
    if (metric == "exact_overlap_count") return indexed ? std::optional<uint64_t>(value.exactOverlaps) : std::nullopt;
    if (metric == "pair_add_count") return collision ? std::optional<uint64_t>(value.pairAdds) : std::nullopt;
    if (metric == "pair_remove_count") return collision ? std::optional<uint64_t>(value.pairRemoves) : std::nullopt;
    if (metric == "rebuild_call_count") return bvh ? std::optional<uint64_t>(value.rebuildCalls) : std::nullopt;
    if (metric == "rebuild_item_count") return bvh ? std::optional<uint64_t>(value.rebuildItems) : std::nullopt;
    if (metric == "tree_height") return bvh ? std::optional<uint64_t>(value.treeHeight) : std::nullopt;
    if (metric == "tree_node_count") return bvh ? std::optional<uint64_t>(value.treeNodes) : std::nullopt;
    if (metric == "sprite_count") return visibility ? std::optional<uint64_t>(value.spriteCount) : std::nullopt;
    if (metric == "visible_candidate_count") return visibility ? std::optional<uint64_t>(value.visibleCandidates) : std::nullopt;
    if (metric == "uncullable_sprite_count") return visibility && indexed ? std::optional<uint64_t>(value.uncullableSprites) : std::nullopt;
    if (metric == "culling_bypass_count") return visibility && indexed ? std::optional<uint64_t>(value.cullingBypasses) : std::nullopt;
    if (metric == "visibility_cache_hit_count") return visibility && indexed ? std::optional<uint64_t>(value.visibilityCacheHits) : std::nullopt;
    if (metric == "visibility_cell_count")
        return visibility && settings.renderCulling == RenderCullingMode::Segmented
                   ? std::optional<uint64_t>(value.visibilityCells) : std::nullopt;
    return std::nullopt;
}

void writeFile(const std::filesystem::path &path, const std::string &contents)
{
    std::ofstream output(path, std::ios::binary);
    output.exceptions(std::ios::badbit | std::ios::failbit);
    output << contents;
    output.close();
}

std::string sha256File(const std::string &path)
{
    static constexpr std::array<uint32_t, 64> constants = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Unable to hash spatial trace: " + path);
    std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (input.bad()) throw std::runtime_error("Unable to read spatial trace for hashing: " + path);
    std::vector<uint8_t> message(contents.begin(), contents.end());
    const uint64_t bitLength = static_cast<uint64_t>(message.size()) * 8;
    message.push_back(0x80);
    while ((message.size() % 64) != 56) message.push_back(0);
    for (int shift = 56; shift >= 0; shift -= 8)
        message.push_back(static_cast<uint8_t>((bitLength >> shift) & 0xff));

    std::array<uint32_t, 8> hash = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                                     0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    for (size_t offset = 0; offset < message.size(); offset += 64)
    {
        std::array<uint32_t, 64> words{};
        for (size_t index = 0; index < 16; ++index)
        {
            const size_t at = offset + index * 4;
            words[index] = (uint32_t(message[at]) << 24) | (uint32_t(message[at + 1]) << 16) |
                           (uint32_t(message[at + 2]) << 8) | uint32_t(message[at + 3]);
        }
        for (size_t index = 16; index < words.size(); ++index)
        {
            const uint32_t s0 = std::rotr(words[index - 15], 7) ^ std::rotr(words[index - 15], 18) ^ (words[index - 15] >> 3);
            const uint32_t s1 = std::rotr(words[index - 2], 17) ^ std::rotr(words[index - 2], 19) ^ (words[index - 2] >> 10);
            words[index] = words[index - 16] + s0 + words[index - 7] + s1;
        }
        auto [a,b,c,d,e,f,g,h] = hash;
        for (size_t index = 0; index < words.size(); ++index)
        {
            const uint32_t s1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
            const uint32_t choice = (e & f) ^ (~e & g);
            const uint32_t temp1 = h + s1 + choice + constants[index] + words[index];
            const uint32_t s0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
            const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = s0 + majority;
            h = g; g = f; f = e; e = d + temp1; d = c; c = b; b = a; a = temp1 + temp2;
        }
        hash[0] += a; hash[1] += b; hash[2] += c; hash[3] += d;
        hash[4] += e; hash[5] += f; hash[6] += g; hash[7] += h;
    }
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (uint32_t word : hash) output << std::setw(8) << word;
    return output.str();
}
} // namespace

void writeSpatialReplayOutputs(const std::string &requestedPath, const std::string &tracePath, const SpatialTrace &trace,
                               const SpatialSettings &settings, const SpatialReplayRun &run,
                               bool validationMode, bool resourceMode, nlohmann::json resourceData)
{
    const std::filesystem::path path(requestedPath);
    if (path.empty()) throw std::invalid_argument("Spatial replay output path is empty");
    if (run.measuredSamples.size() != trace.header.measureSteps)
        throw std::runtime_error("Spatial replay measured sample count is incomplete");
    try
    {
        const std::string absoluteTracePath = std::filesystem::absolute(tracePath).string();
        const std::string traceHash = sha256File(absoluteTracePath);
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        std::ostringstream summary;
        summary << std::setprecision(12) << "schema_version,category,name,samples,avg,median,p95,p99,min,max,unit\n";
        const auto summaryRow = [&](const char *category, const char *name, std::vector<double> values, const char *unit)
        {
            if (values.empty()) { summary << "4," << category << ',' << name << ",0,,,,,,," << unit << '\n'; return; }
            const auto stats = summarize(values);
            summary << "4," << category << ',' << name << ',' << values.size() << ',' << stats.avg << ',' << stats.median
                    << ',' << stats.p95 << ',' << stats.p99 << ',' << stats.minimum << ',' << stats.maximum << ',' << unit << '\n';
        };
        for (const char *name : scopes)
        {
            std::vector<double> values;
            if (scopeApplies(name, trace, settings))
                for (const auto &sample : run.measuredSamples)
                    values.push_back(static_cast<double>(*scopeValue(name, sample.counters, &sample, trace, settings)) / 1e6);
            summaryRow("scope", name, std::move(values), "ms");
        }
        for (const char *name : counters)
        {
            std::vector<double> values;
            for (const auto &sample : run.measuredSamples)
                if (auto value = counterValue(name, sample.counters, trace, settings)) values.push_back(static_cast<double>(*value));
            summaryRow("counter", name, std::move(values), "count");
        }

        std::ostringstream raw;
        raw << "schema_version,sample_kind,index";
        for (const char *name : scopes) raw << ',' << name << "_ns";
        for (const char *name : counters) raw << ',' << name;
        raw << '\n';
        for (size_t index = 0; index < run.measuredSamples.size(); ++index)
        {
            const auto &sample = run.measuredSamples[index];
            raw << "4,replay," << index;
            for (const char *name : scopes) { raw << ','; if (auto value = scopeValue(name, sample.counters, &sample, trace, settings)) raw << *value; }
            for (const char *name : counters) { raw << ','; if (auto value = counterValue(name, sample.counters, trace, settings)) raw << *value; }
            raw << '\n';
        }

        nlohmann::json metrics = nlohmann::json::object();
        for (const char *name : scopes) metrics[name] = scopeApplies(name, trace, settings) ? "applicable" : "inapplicable";
        for (const char *name : counters) metrics[name] = counterValue(name, run.initialCounters, trace, settings).has_value() ? "applicable" : "inapplicable";
        const bool collision = trace.header.domain == SpatialTraceDomain::Collision;
        const std::string broad = broadName(settings.broadPhase), render = renderName(settings.renderCulling);
        const int64_t workers = tbb::global_control::active_value(tbb::global_control::max_allowed_parallelism);
        nlohmann::json hashes = nlohmann::json::array();
        if (validationMode) { hashes.push_back(run.initialOutputHash); for (const auto &sample : run.measuredSamples) hashes.push_back(sample.outputHash); }
        nlohmann::json manifest = nlohmann::json::object();
        manifest["schema_version"] = 4;
        manifest["capture_mode"] = resourceMode ? "resource" : validationMode ? "validate" : "performance";
        manifest["timing_eligible"] = !validationMode && !resourceMode;
        manifest["complete_capture"] = true;
        manifest["status"] = "complete";
        manifest["run"] = {{"warmup_steps", trace.header.warmupSteps},
                            {"requested_measured_steps", trace.header.measureSteps},
                            {"measured_steps", run.measuredSamples.size()}, {"seed", trace.header.seed},
                            {"dt", trace.header.dt}, {"workers_requested", workers}, {"workers_effective", workers}};
        manifest["input"] = {{"domain", collision ? "collision-replay" : "visibility-replay"},
                              {"case_id", trace.header.caseId}, {"case_sha256", traceHash},
                              {"case_sha256_status", "available"}, {"trace_path", absoluteTracePath},
                              {"generator", trace.header.generator}, {"parameters", trace.header.parameters},
                              {"bounds_source", trace.header.parameters.at("bounds_source")},
                              {"geometry_preparation", collision ? "excluded; trace supplies exact AABBs" : "included; trace supplies transforms"}};
        manifest["selection"] = {{"requested", {{"broad_phase", broad}, {"render_culling", render},
                                                       {"bvh_maintenance", settings.bvhMaintenance == BvhMaintenance::NativePartial ? "native-partial" : "full-rebuild"},
                                                       {"bvh_padding", settings.bvhPadding},
                                                       {"bvh_query_execution", settings.bvhQueryExecution == BvhQueryExecution::Parallel ? "parallel" : "serial"}}},
                                 {"effective", {{"broad_phase", broad}, {"render_culling", render}}}};
        manifest["sample_counts"] = {{"warmup", trace.header.warmupSteps}, {"step", 0}, {"render", 0},
                                     {"measured_replay", run.measuredSamples.size()},
                                     {"actual_render", 0}};
        manifest["applicability"] = {{"sample_kind", "replay"},
                                     {"replay_domain", collision ? "collision" : "visibility"},
                                     {"metrics", std::move(metrics)},
                                     {"units", {{"scopes", "ns"}, {"counters", "count"}}}};
        manifest["boundaries"] = {{"spatial_replay", "preloaded operations through selected-domain consumer output; excludes parsing, SDL and validation oracles"},
                                   {"bounds_prepare", "visibility transforms to conservative bounds; collision AABBs are supplied"},
                                   {"index_update", "selected index writes and segmented membership work"},
                                   {"index_maintenance", "separately scheduled BVH rebuild work"},
                                   {"index_query", "read-only linear/tree queries; segmented discovery stays in index_update"},
                                   {"result_reduce", "exact refinement, pair/visible result updates and order restoration"}};
        manifest["initialization"] = {{"total_ns", run.initializationNanoseconds},
                                      {"bounds_prepare_ns", run.initialCounters.boundsPrepareNanoseconds},
                                      {"index_update_ns", run.initialCounters.indexUpdateNanoseconds},
                                      {"index_maintenance_ns", run.initialCounters.indexMaintenanceNanoseconds},
                                      {"index_query_ns", run.initialCounters.indexQueryNanoseconds},
                                      {"result_reduce_ns", run.initialCounters.resultReduceNanoseconds},
                                      {"initial_counters", {{"spatial_proxy_count", run.initialCounters.proxyCount},
                                                             {"candidate_pair_count", run.initialCounters.candidatePairCount},
                                                             {"sprite_count", run.initialCounters.spriteCount},
                                                             {"visible_candidate_count", run.initialCounters.visibleCandidates}}}};
        manifest["validation"] = {{"status", validationMode ? "passed" : "not_run"},
                                  {"output_hashes", std::move(hashes)},
                                  {"timing_eligible", !validationMode && !resourceMode}};
        manifest["resources"] = resourceMode ? std::move(resourceData)
                                              : nlohmann::json{{"status", "unavailable"}, {"reason", "resource-only runner not used"}};
        manifest["build"] = {{"commit", PLATFORMATOR_BENCH_COMMIT}, {"dirty", bool(PLATFORMATOR_BENCH_DIRTY)},
                              {"source_sha256", PLATFORMATOR_BENCH_SOURCE_SHA256}, {"compiler", PLATFORMATOR_BENCH_COMPILER},
                              {"flags", PLATFORMATOR_BENCH_FLAGS}, {"configuration", PLATFORMATOR_BENCH_CONFIGURATION},
                              {"sanitizer", PLATFORMATOR_BENCH_SANITIZER},
                              {"work_probes", bool(PLATFORMATOR_ENABLE_WORK_PROBES)},
                              {"debug_tools", bool(PLATFORMATOR_ENABLE_DEBUG_TOOLS)},
                              {"resource_hooks", resourceMode}};
        manifest["machine"] = {{"logical_cpus", std::thread::hardware_concurrency()}};
        manifest["dependencies"] = {{"SDL", std::to_string(SDL_VERSIONNUM_MAJOR(SDL_GetVersion())) + '.' +
                                               std::to_string(SDL_VERSIONNUM_MINOR(SDL_GetVersion())) + '.' +
                                               std::to_string(SDL_VERSIONNUM_MICRO(SDL_GetVersion()))},
                                     {"oneTBB", "2022.3.0"}};
        auto samplesPath = path; samplesPath.replace_extension(".samples.csv");
        auto manifestPath = path; manifestPath.replace_extension(".manifest.json");
        writeFile(path, summary.str());
        writeFile(samplesPath, raw.str());
        writeFile(manifestPath, manifest.dump(2) + '\n');
    }
    catch (const std::exception &error)
    {
        throw std::runtime_error("Spatial replay output '" + path.string() + "': " + error.what());
    }
}
} // namespace platformator_detail
#endif
