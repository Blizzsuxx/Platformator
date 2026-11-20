#pragma once

#include <bit>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace solver_measurement
{
using Clock = std::chrono::steady_clock;
using Nanoseconds = std::chrono::nanoseconds;

struct Sample
{
    long long preparation = 0;
    long long grouping = 0;
    long long iterations = 0;
    size_t islandCount = 0;
    size_t largestIsland = 0;
    size_t islandsDispatched = 0;
    size_t batchesDispatched = 0;
    size_t parityChecks = 0;
};

inline Sample sample;
inline const std::string_view mode = [] {
    const auto *value = std::getenv("PLATFORMATOR_IMPLEMENTATION_MODE");
    return value ? std::string_view(value) : std::string_view{};
}();

struct Timer
{
    long long &duration;
    Clock::time_point begin = Clock::now();
    ~Timer()
    {
        duration += std::chrono::duration_cast<Nanoseconds>(Clock::now() - begin).count();
    }
};

struct Capture
{
    std::vector<Sample> rows;
    ~Capture()
    {
        if (const auto *path = std::getenv("PLATFORMATOR_IMPLEMENTATION_PROFILE"))
        {
            std::ofstream file(path);
            file << "preparation_ns,grouping_ns,iterations_ns,island_count,largest_island,"
                    "islands_dispatched,batches_dispatched,parity_checks\n";
            for (const auto &row : rows)
            {
                file << row.preparation << ',' << row.grouping << ',' << row.iterations << ',' << row.islandCount
                     << ',' << row.largestIsland << ',' << row.islandsDispatched << ',' << row.batchesDispatched
                     << ',' << row.parityChecks << '\n';
            }
        }
    }
};

inline Capture capture;
}
