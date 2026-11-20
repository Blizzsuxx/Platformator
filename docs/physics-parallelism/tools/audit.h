#pragma once

#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <string_view>
#include <vector>

namespace audit
{
enum Field
{
    SyncPre, SyncPost, SyncCleanup, PreDirty, PostDirty, CleanupDirty,
    Geometry, Commit, SupportFlush, Preparation, Schedule, Iterations, Sleep,
    Constraints, Skip, WritableConstraints, LargestIsland, GreedyColors,
    LargestColor, GreedyOverflow, OrderedLevels, LargestLevel, FieldCount
};
inline constexpr std::array names{
    "sync_pre_ns", "sync_post_ns", "sync_cleanup_ns", "pre_dirty", "post_dirty", "cleanup_dirty",
    "geometry_ns", "commit_ns", "support_flush_ns", "preparation_ns", "schedule_ns", "iterations_ns", "sleep_ns",
    "constraints", "skip", "writable_constraints", "largest_island", "greedy_colors",
    "largest_color", "greedy_overflow", "ordered_levels", "largest_level"
};
static_assert(names.size() == FieldCount);
using Clock = std::chrono::steady_clock;
inline auto now() { return Clock::now(); }
inline long long elapsed(Clock::time_point start)
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now() - start).count();
}
inline std::array<long long, FieldCount> sample{};
inline const bool graph = std::getenv("PLATFORMATOR_AUDIT_GRAPH") != nullptr;
inline const bool shortCommit = std::getenv("PLATFORMATOR_AUDIT_SHORT_COMMIT") != nullptr;
inline const auto solver = [] {
    const auto *value = std::getenv("PLATFORMATOR_AUDIT_SOLVER");
    return value ? std::string_view(value) : std::string_view{};
}();
inline const int containers = std::getenv("PLATFORMATOR_AUDIT_PILES") ? 8 : 1;
struct Samples
{
    std::vector<std::array<long long, FieldCount>> rows;
    ~Samples()
    {
        if (const auto *path = std::getenv("PLATFORMATOR_AUDIT_SAMPLES"))
        {
            std::ofstream out(path);
            out << "step";
            for (const auto *name : names)
                out << ',' << name;
            out << '\n';
            for (size_t i = 0; i < rows.size(); ++i)
            {
                out << i + 1;
                for (auto value : rows[i])
                    out << ',' << value;
                out << '\n';
            }
        }
    }
};
inline Samples samples;
}
