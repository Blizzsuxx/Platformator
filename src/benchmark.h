#pragma once

#include "buildconfig.h"

#if PLATFORMATOR_ENABLE_BENCHMARKS

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace platformator_detail
{
    enum class BenchmarkScopeId : uint8_t
    {
        Frame,
        BroadPhase,
        NarrowPhase,
        ResolveCollisions,
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
        CollisionStayEventCount,
        CollisionExitEventCount,
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
        void beginFrame();
        void endFrame();
        void addScopeDuration(BenchmarkScopeId scopeId, uint64_t elapsedNanoseconds);
        void setCounter(BenchmarkCounterId counterId, int64_t value);
        void addCounter(BenchmarkCounterId counterId, int64_t delta);
        void printSummary(FILE *output = stdout) const;

    private:
        BenchmarkRecorder();

        bool frameActive;
        std::chrono::steady_clock::time_point frameStartTime;
        BenchmarkFrameStats currentFrame;
        std::vector<BenchmarkFrameStats> frames;
    };

    class BenchmarkScopeTimer
    {
    public:
        explicit BenchmarkScopeTimer(BenchmarkScopeId scopeId);
        ~BenchmarkScopeTimer();

    private:
        BenchmarkScopeId scopeId;
        std::chrono::steady_clock::time_point startTime;
    };
} // namespace platformator_detail

#define PLATFORMATOR_BENCH_CONCAT_INNER(left, right) left##right
#define PLATFORMATOR_BENCH_CONCAT(left, right) PLATFORMATOR_BENCH_CONCAT_INNER(left, right)
#define PLATFORMATOR_BENCH_SCOPE(scopeId) platformator_detail::BenchmarkScopeTimer PLATFORMATOR_BENCH_CONCAT(platformatorBenchmarkScopeTimer_, __LINE__)(platformator_detail::BenchmarkScopeId::scopeId)
#define PLATFORMATOR_BENCH_FRAME_BEGIN() platformator_detail::BenchmarkRecorder::getInstance().beginFrame()
#define PLATFORMATOR_BENCH_FRAME_END() platformator_detail::BenchmarkRecorder::getInstance().endFrame()
#define PLATFORMATOR_BENCH_SET_COUNTER(counterId, value) platformator_detail::BenchmarkRecorder::getInstance().setCounter(platformator_detail::BenchmarkCounterId::counterId, static_cast<int64_t>(value))
#define PLATFORMATOR_BENCH_ADD_COUNTER(counterId, delta) platformator_detail::BenchmarkRecorder::getInstance().addCounter(platformator_detail::BenchmarkCounterId::counterId, static_cast<int64_t>(delta))
#define PLATFORMATOR_BENCH_RESET() platformator_detail::BenchmarkRecorder::getInstance().reset()
#define PLATFORMATOR_BENCH_PRINT_SUMMARY() platformator_detail::BenchmarkRecorder::getInstance().printSummary()

#else

#define PLATFORMATOR_BENCH_SCOPE(scopeId) ((void)0)
#define PLATFORMATOR_BENCH_FRAME_BEGIN() ((void)0)
#define PLATFORMATOR_BENCH_FRAME_END() ((void)0)
#define PLATFORMATOR_BENCH_SET_COUNTER(counterId, value) ((void)0)
#define PLATFORMATOR_BENCH_ADD_COUNTER(counterId, delta) ((void)0)
#define PLATFORMATOR_BENCH_RESET() ((void)0)
#define PLATFORMATOR_BENCH_PRINT_SUMMARY() ((void)0)

#endif