#pragma once
#include "buildconfig.h"

#if PLATFORMATOR_ENABLE_BENCHMARKS
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <json.hpp>

#include "spatialreplay.h"
#include "platformator/spatialsettings.h"

namespace platformator_detail
{
enum class BenchmarkScopeId : uint8_t
{
    CompleteStep,
    Physics,
    Cleanup,
    SceneTransition,
    Render,
    BroadPhase,
    NarrowPhase,
    ResolveCollisions,
    SpatialUpdate,
    RenderCulling,
    RenderSubmit,
    SpatialReplay,
    BoundsPrepare,
    IndexUpdate,
    IndexMaintenance,
    IndexQuery,
    ResultReduce,
    COUNT
};
enum class BenchmarkCounterId : uint8_t
{
    ObjectCount,
    OccupiedCellCount,
    CandidatePairCount,
    PendingNarrowPhasePairCount,
    ActiveCollisionCount,
    AwakeDynamicBodyCount,
    QueuedAddCount,
    QueuedSyncCount,
    QueuedRemoveCount,
    CollisionEnterEventCount,
    CollisionExitEventCount,
    SpatialProxyCount,
    ChangedBoundsCount,
    FilterChangeCount,
    ProxyInsertCount,
    ProxyRemoveCount,
    ProxyMoveCount,
    EnvelopeReuseCount,
    QueryCount,
    RawDiscoveryCount,
    ExactOverlapCount,
    PairAddCount,
    PairRemoveCount,
    RebuildCallCount,
    RebuildItemCount,
    TreeHeight,
    TreeNodeCount,
    SpriteCount,
    VisibleCandidateCount,
    SubmittedSpriteCount,
    UncullableSpriteCount,
    CullingBypassCount,
    VisibilityCacheHitCount,
    VisibilityCellCount,
    COUNT
};
constexpr size_t BENCHMARK_SCOPE_COUNT = static_cast<size_t>(BenchmarkScopeId::COUNT);
constexpr size_t BENCHMARK_COUNTER_COUNT = static_cast<size_t>(BenchmarkCounterId::COUNT);
struct BenchmarkFrameStats
{
    std::array<uint64_t, BENCHMARK_SCOPE_COUNT> scopeDurationsNanoseconds{};
    std::array<int64_t, BENCHMARK_COUNTER_COUNT> counters{};
};
class BenchmarkRecorder
{
  public:
    static BenchmarkRecorder &getInstance();
    void reset();
    void configure(size_t expectedSamples, nlohmann::json metadata);
    void beginFrame();
    void endFrame();
    void setInitializationActive(bool active);
    uint64_t initializationScopeNanoseconds(BenchmarkScopeId scope) const;
    int64_t initializationCounterValue(BenchmarkCounterId counter) const;
    void beginRender();
    void cancelRenderSample();
    void enterScope(BenchmarkScopeId scope);
    void leaveScope(BenchmarkScopeId scope, uint64_t nanoseconds);
    bool scopeActive(BenchmarkScopeId scope) const;
    void addScopeDuration(BenchmarkScopeId scope, uint64_t nanoseconds);
    void setCounter(BenchmarkCounterId counter, int64_t value);
    void addCounter(BenchmarkCounterId counter, int64_t delta);
    void setCsvOutputPath(const std::string &path);
    void clearCsvOutputPath();
    void updateMetadata(const nlohmann::json &metadata);
    void writeOutputs(const std::string &path = {}) const;
    void printSummary(FILE *output = stdout) const;
    const std::vector<BenchmarkFrameStats> &samples() const
    {
        return frames;
    }
    size_t sampleCapacity() const
    {
        return frames.capacity();
    }
    size_t renderSampleCount() const
    {
        return renders.size();
    }

  private:
    BenchmarkRecorder();
    nlohmann::json manifest() const;
    bool frameActive = false;
    size_t nextFrame = 0, nextRender = 0;
    uint64_t totalSteps = 0;
    uint64_t totalRenders = 0;
    std::chrono::steady_clock::time_point frameStartTime;
    BenchmarkFrameStats currentFrame;
    BenchmarkFrameStats currentRender;
    bool renderActive = false;
    bool suppressCurrentRender = false;
    bool initializationActive = false;
    BenchmarkFrameStats initializationStats;
    std::array<unsigned, BENCHMARK_SCOPE_COUNT> activeScopes{};
    std::vector<BenchmarkFrameStats> frames;
    std::vector<BenchmarkFrameStats> renders;
    nlohmann::json metadata;
    std::string csvOutputPath;
};

void writeSpatialReplayOutputs(const std::string &path, const std::string &tracePath, const SpatialTrace &trace,
                               const SpatialSettings &settings, const SpatialReplayRun &run, bool validationMode,
                               bool resourceMode = false, nlohmann::json resourceData = nlohmann::json::object());
class BenchmarkScopeTimer
{
  public:
    explicit BenchmarkScopeTimer(BenchmarkScopeId scope, bool enabled = true);
    ~BenchmarkScopeTimer();

  private:
    BenchmarkScopeId scope;
    bool enabled;
    std::chrono::steady_clock::time_point started;
};
} // namespace platformator_detail
#define PLATFORMATOR_BENCH_CONCAT_INNER(a, b) a##b
#define PLATFORMATOR_BENCH_CONCAT(a, b) PLATFORMATOR_BENCH_CONCAT_INNER(a, b)
#define PLATFORMATOR_BENCH_SCOPE(scope)                                                                                \
    platformator_detail::BenchmarkScopeTimer PLATFORMATOR_BENCH_CONCAT(benchmarkScope_, __LINE__)(                     \
        platformator_detail::BenchmarkScopeId::scope)
#define PLATFORMATOR_BENCH_SCOPE_IF(scope, enabled)                                                                    \
    platformator_detail::BenchmarkScopeTimer PLATFORMATOR_BENCH_CONCAT(benchmarkScope_, __LINE__)(                     \
        platformator_detail::BenchmarkScopeId::scope, enabled)
#define PLATFORMATOR_BENCH_BEGIN_RENDER() platformator_detail::BenchmarkRecorder::getInstance().beginRender()
#define PLATFORMATOR_BENCH_CANCEL_RENDER() platformator_detail::BenchmarkRecorder::getInstance().cancelRenderSample()
#define PLATFORMATOR_BENCH_ADD_SCOPE_DURATION(scope, nanoseconds)                                                       \
    platformator_detail::BenchmarkRecorder::getInstance().addScopeDuration(                                            \
        platformator_detail::BenchmarkScopeId::scope, static_cast<uint64_t>(nanoseconds))
#define PLATFORMATOR_BENCH_FRAME_BEGIN() platformator_detail::BenchmarkRecorder::getInstance().beginFrame()
#define PLATFORMATOR_BENCH_FRAME_END() platformator_detail::BenchmarkRecorder::getInstance().endFrame()
#define PLATFORMATOR_BENCH_SET_COUNTER(counter, value)                                                                 \
    platformator_detail::BenchmarkRecorder::getInstance().setCounter(platformator_detail::BenchmarkCounterId::counter, \
                                                                     static_cast<int64_t>(value))
#define PLATFORMATOR_BENCH_ADD_COUNTER(counter, delta)                                                                 \
    platformator_detail::BenchmarkRecorder::getInstance().addCounter(platformator_detail::BenchmarkCounterId::counter, \
                                                                     static_cast<int64_t>(delta))
#define PLATFORMATOR_BENCH_RESET() platformator_detail::BenchmarkRecorder::getInstance().reset()
#define PLATFORMATOR_BENCH_SET_CSV_OUTPUT_PATH(path)                                                                   \
    platformator_detail::BenchmarkRecorder::getInstance().setCsvOutputPath(path)
#define PLATFORMATOR_BENCH_CLEAR_CSV_OUTPUT_PATH()                                                                     \
    platformator_detail::BenchmarkRecorder::getInstance().clearCsvOutputPath()
#define PLATFORMATOR_BENCH_PRINT_SUMMARY() platformator_detail::BenchmarkRecorder::getInstance().printSummary()
#else
#define PLATFORMATOR_BENCH_SCOPE(scope) ((void)0)
#define PLATFORMATOR_BENCH_SCOPE_IF(scope, enabled) ((void)0)
#define PLATFORMATOR_BENCH_BEGIN_RENDER() ((void)0)
#define PLATFORMATOR_BENCH_CANCEL_RENDER() ((void)0)
#define PLATFORMATOR_BENCH_ADD_SCOPE_DURATION(scope, nanoseconds) ((void)0)
#define PLATFORMATOR_BENCH_FRAME_BEGIN() ((void)0)
#define PLATFORMATOR_BENCH_FRAME_END() ((void)0)
#define PLATFORMATOR_BENCH_SET_COUNTER(counter, value) ((void)0)
#define PLATFORMATOR_BENCH_ADD_COUNTER(counter, delta) ((void)0)
#define PLATFORMATOR_BENCH_RESET() ((void)0)
#define PLATFORMATOR_BENCH_SET_CSV_OUTPUT_PATH(path) ((void)0)
#define PLATFORMATOR_BENCH_CLEAR_CSV_OUTPUT_PATH() ((void)0)
#define PLATFORMATOR_BENCH_PRINT_SUMMARY() ((void)0)
#endif
