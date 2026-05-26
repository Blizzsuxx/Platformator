#include "benchmark.h"

#if PLATFORMATOR_ENABLE_BENCHMARKS

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <cmath>

namespace
{
    double nanosecondsToMilliseconds(const uint64_t nanoseconds)
    {
        return static_cast<double>(nanoseconds) / 1000000.0;
    }

    size_t percentileIndex(const size_t sampleCount, const double percentile)
    {
        return static_cast<size_t>(std::ceil(percentile * static_cast<double>(sampleCount))) - 1;
    }

    const char *scopeName(const platformator_detail::BenchmarkScopeId scopeId)
    {
        switch (scopeId)
        {
        case platformator_detail::BenchmarkScopeId::Frame:
            return "frame";
        case platformator_detail::BenchmarkScopeId::BroadPhase:
            return "broad_phase";
        case platformator_detail::BenchmarkScopeId::NarrowPhase:
            return "narrow_phase";
        case platformator_detail::BenchmarkScopeId::ResolveCollisions:
            return "resolve_collisions";
        case platformator_detail::BenchmarkScopeId::COUNT:
            return "unknown";
        }

        return "unknown";
    }

    const char *counterName(const platformator_detail::BenchmarkCounterId counterId)
    {
        switch (counterId)
        {
        case platformator_detail::BenchmarkCounterId::ObjectCount:
            return "object_count";
        case platformator_detail::BenchmarkCounterId::OccupiedCellCount:
            return "occupied_cell_count";
        case platformator_detail::BenchmarkCounterId::CandidatePairCount:
            return "candidate_pair_count";
        case platformator_detail::BenchmarkCounterId::PendingNarrowPhasePairCount:
            return "pending_narrow_phase_pair_count";
        case platformator_detail::BenchmarkCounterId::ActiveCollisionCount:
            return "active_collision_count";
        case platformator_detail::BenchmarkCounterId::AwakeDynamicBodyCount:
            return "awake_dynamic_body_count";
        case platformator_detail::BenchmarkCounterId::QueuedAddCount:
            return "queued_add_count";
        case platformator_detail::BenchmarkCounterId::QueuedSyncCount:
            return "queued_sync_count";
        case platformator_detail::BenchmarkCounterId::QueuedRemoveCount:
            return "queued_remove_count";
        case platformator_detail::BenchmarkCounterId::CollisionEnterEventCount:
            return "collision_enter_event_count";
        case platformator_detail::BenchmarkCounterId::CollisionStayEventCount:
            return "collision_stay_event_count";
        case platformator_detail::BenchmarkCounterId::CollisionExitEventCount:
            return "collision_exit_event_count";
        case platformator_detail::BenchmarkCounterId::COUNT:
            return "unknown";
        }

        return "unknown";
    }
} // namespace

namespace platformator_detail
{
    BenchmarkRecorder &BenchmarkRecorder::getInstance()
    {
        static BenchmarkRecorder benchmarkRecorder;
        return benchmarkRecorder;
    }

    BenchmarkRecorder::BenchmarkRecorder()
        : frameActive(false), frameStartTime(), currentFrame(), frames(), csvOutputPath()
    {
    }

    void BenchmarkRecorder::reset()
    {
        frameActive = false;
        currentFrame = BenchmarkFrameStats{};
        frames.clear();
    }

    void BenchmarkRecorder::beginFrame()
    {
        currentFrame = BenchmarkFrameStats{};
        frameStartTime = std::chrono::steady_clock::now();
        frameActive = true;
    }

    void BenchmarkRecorder::endFrame()
    {
        if (!frameActive)
        {
            return;
        }

        const auto elapsedNanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - frameStartTime).count();
        currentFrame.scopeDurationsNanoseconds[static_cast<size_t>(BenchmarkScopeId::Frame)] = static_cast<uint64_t>(elapsedNanoseconds);
        frames.push_back(currentFrame);
        frameActive = false;
    }

    void BenchmarkRecorder::addScopeDuration(const BenchmarkScopeId scopeId, const uint64_t elapsedNanoseconds)
    {
        if (!frameActive)
        {
            return;
        }

        currentFrame.scopeDurationsNanoseconds[static_cast<size_t>(scopeId)] += elapsedNanoseconds;
    }

    void BenchmarkRecorder::setCounter(const BenchmarkCounterId counterId, const int64_t value)
    {
        if (!frameActive)
        {
            return;
        }

        currentFrame.counters[static_cast<size_t>(counterId)] = value;
    }

    void BenchmarkRecorder::addCounter(const BenchmarkCounterId counterId, const int64_t delta)
    {
        if (!frameActive)
        {
            return;
        }

        currentFrame.counters[static_cast<size_t>(counterId)] += delta;
    }

    void BenchmarkRecorder::setCsvOutputPath(const std::string &path)
    {
        csvOutputPath = path;
    }

    void BenchmarkRecorder::clearCsvOutputPath()
    {
        csvOutputPath.clear();
    }

    void BenchmarkRecorder::printSummary(FILE *output) const
    {
        if (frames.empty())
        {
            return;
        }

        std::fprintf(output, "[Benchmark] frame_count=%zu\n", frames.size());

        for (size_t scopeIndex = 0; scopeIndex < BENCHMARK_SCOPE_COUNT; ++scopeIndex)
        {
            std::vector<uint64_t> samples;
            samples.reserve(frames.size());

            long double totalNanoseconds = 0.0;
            for (const BenchmarkFrameStats &frame : frames)
            {
                const uint64_t sample = frame.scopeDurationsNanoseconds[scopeIndex];
                samples.push_back(sample);
                totalNanoseconds += static_cast<long double>(sample);
            }

            std::sort(samples.begin(), samples.end());

            const uint64_t medianSample = samples[samples.size() / 2];
            const uint64_t p95Sample = samples[percentileIndex(samples.size(), 0.95)];
            const uint64_t p99Sample = samples[percentileIndex(samples.size(), 0.99)];
            const uint64_t minimumSample = samples.front();
            const uint64_t maximumSample = samples.back();
            const double averageMilliseconds = static_cast<double>(totalNanoseconds / static_cast<long double>(frames.size())) / 1000000.0;

            std::fprintf(
                output,
                "[Benchmark][Scope] %s avg_ms=%.6f median_ms=%.6f p95_ms=%.6f p99_ms=%.6f min_ms=%.6f max_ms=%.6f\n",
                scopeName(static_cast<BenchmarkScopeId>(scopeIndex)),
                averageMilliseconds,
                nanosecondsToMilliseconds(medianSample),
                nanosecondsToMilliseconds(p95Sample),
                nanosecondsToMilliseconds(p99Sample),
                nanosecondsToMilliseconds(minimumSample),
                nanosecondsToMilliseconds(maximumSample));
        }

        for (size_t counterIndex = 0; counterIndex < BENCHMARK_COUNTER_COUNT; ++counterIndex)
        {
            int64_t minimumValue = frames.front().counters[counterIndex];
            int64_t maximumValue = minimumValue;
            long double totalValue = 0.0;

            for (const BenchmarkFrameStats &frame : frames)
            {
                const int64_t sample = frame.counters[counterIndex];
                minimumValue = std::min(minimumValue, sample);
                maximumValue = std::max(maximumValue, sample);
                totalValue += static_cast<long double>(sample);
            }

            const double averageValue = static_cast<double>(totalValue / static_cast<long double>(frames.size()));

            std::fprintf(
                output,
                "[Benchmark][Counter] %s avg=%.3f min=%lld max=%lld\n",
                counterName(static_cast<BenchmarkCounterId>(counterIndex)),
                averageValue,
                static_cast<long long>(minimumValue),
                static_cast<long long>(maximumValue));
        }

        if (!csvOutputPath.empty())
        {
            FILE *csvFile = std::fopen(csvOutputPath.c_str(), "w");
            if (csvFile == nullptr)
            {
                std::fprintf(
                    stderr,
                    "[Benchmark][Warning] Failed to open CSV output '%s': %s\n",
                    csvOutputPath.c_str(),
                    std::strerror(errno));
                return;
            }

            std::fprintf(csvFile, "category,name,avg,median,p95,p99,min,max,unit\n");
            std::fprintf(csvFile, "meta,frame_count,%zu,,,,,,frames\n", frames.size());

            for (size_t scopeIndex = 0; scopeIndex < BENCHMARK_SCOPE_COUNT; ++scopeIndex)
            {
                std::vector<uint64_t> samples;
                samples.reserve(frames.size());

                long double totalNanoseconds = 0.0;
                for (const BenchmarkFrameStats &frame : frames)
                {
                    const uint64_t sample = frame.scopeDurationsNanoseconds[scopeIndex];
                    samples.push_back(sample);
                    totalNanoseconds += static_cast<long double>(sample);
                }

                std::sort(samples.begin(), samples.end());

                const uint64_t medianSample = samples[samples.size() / 2];
                const uint64_t p95Sample = samples[percentileIndex(samples.size(), 0.95)];
                const uint64_t p99Sample = samples[percentileIndex(samples.size(), 0.99)];
                const uint64_t minimumSample = samples.front();
                const uint64_t maximumSample = samples.back();
                const double averageMilliseconds = static_cast<double>(totalNanoseconds / static_cast<long double>(frames.size())) / 1000000.0;

                std::fprintf(
                    csvFile,
                    "scope,%s,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,ms\n",
                    scopeName(static_cast<BenchmarkScopeId>(scopeIndex)),
                    averageMilliseconds,
                    nanosecondsToMilliseconds(medianSample),
                    nanosecondsToMilliseconds(p95Sample),
                    nanosecondsToMilliseconds(p99Sample),
                    nanosecondsToMilliseconds(minimumSample),
                    nanosecondsToMilliseconds(maximumSample));
            }

            for (size_t counterIndex = 0; counterIndex < BENCHMARK_COUNTER_COUNT; ++counterIndex)
            {
                int64_t minimumValue = frames.front().counters[counterIndex];
                int64_t maximumValue = minimumValue;
                long double totalValue = 0.0;

                for (const BenchmarkFrameStats &frame : frames)
                {
                    const int64_t sample = frame.counters[counterIndex];
                    minimumValue = std::min(minimumValue, sample);
                    maximumValue = std::max(maximumValue, sample);
                    totalValue += static_cast<long double>(sample);
                }

                const double averageValue = static_cast<double>(totalValue / static_cast<long double>(frames.size()));

                std::fprintf(
                    csvFile,
                    "counter,%s,%.9f,,,,%lld,%lld,count\n",
                    counterName(static_cast<BenchmarkCounterId>(counterIndex)),
                    averageValue,
                    static_cast<long long>(minimumValue),
                    static_cast<long long>(maximumValue));
            }

            std::fclose(csvFile);
            std::fprintf(output, "[Benchmark] csv_output=%s\n", csvOutputPath.c_str());
        }
    }

    BenchmarkScopeTimer::BenchmarkScopeTimer(const BenchmarkScopeId scopeId)
        : scopeId(scopeId), startTime(std::chrono::steady_clock::now())
    {
    }

    BenchmarkScopeTimer::~BenchmarkScopeTimer()
    {
        const auto elapsedNanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - startTime).count();
        BenchmarkRecorder::getInstance().addScopeDuration(scopeId, static_cast<uint64_t>(elapsedNanoseconds));
    }
} // namespace platformator_detail

#endif