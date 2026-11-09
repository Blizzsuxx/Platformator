#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#if defined(PLATFORMATOR_EDITOR_WORK_COUNTERS)
#include <atomic>
#endif

namespace platformator_editor::work
{
enum class Counter : std::size_t
{
    SceneSubmissions,                    // Visible sprite draws submitted to the renderer.
    ChangedRecords,                      // Object IDs included in committed change notifications.
    ImageDecodes,                        // Bounded image decode attempts for thumbnails and Scene resources.
    TextureUploads,                      // Decoded SDL textures uploaded on the owner thread.
    ValidityQueries,                     // Fresh filesystem-backed reference validity resolutions.
    DocumentWrites,                      // Atomic scene-file write attempts.
    SceneCaptures,                       // Scene render-target captures performed.
    SceneCacheHits,                      // Static Scene frames served from the cached target.
    HierarchyLabelRows,                  // Hierarchy rows visited while preparing or drawing labels.
    HierarchyLabelStringGrowths,         // Capacity increases in cached hierarchy label strings.
    HierarchyConnectorParentVisits,      // Ancestor/parent entries visited for visible connectors.
    HierarchyConnectorCandidates,        // Connector endpoints considered for the visible row range.
    HierarchyVisibleRows,                // Rows submitted inside the visible hierarchy range.
    HierarchyScratchCapacityGrowths,     // Capacity increases in retained hierarchy scratch buffers.
    AuthoringDescriptorInsertions,       // Object descriptors inserted into the authoring index.
    AuthoringSubtreeScratchGrowths,      // Capacity increases in authoring subtree scratch buffers.
    ValidationFullDocuments,             // Full-document validation requests started.
    ValidationLocalObjects,              // Objects visited by local validation.
    ValidationMergedObjects,             // Local validation records merged into cached results.
    SceneCullTests,                       // Scene sprites tested against the visible viewport.
    SceneCulledSprites,                   // Scene sprites rejected by viewport culling.
    HierarchyConnectors,                  // Hierarchy connector line segments submitted.
    ProjectTiles,                         // Visible Project entries submitted as tiles.
    AssetChanges,                         // Asset change events accepted by the Project panel.
    OwnerAssetChangePaths,                // Logical paths committed by owner-thread asset change handling.
    BatchPropertyTargetsResolved,         // Property targets resolved while validating a batch edit.
    BatchPropertyPatchesCommitted,        // Property patches committed by batch edits.
    BatchOverlapPathComparisons,          // Ordered-path comparisons used to reject duplicate/overlapping batch addresses.
    PreferenceSettingsWriteAttempts,      // Atomic settings-file write attempts (including failed writes).
    PreferenceLayoutWriteAttempts,        // Atomic docking-layout write attempts (including failed writes).
    Count
};

using Counts = std::array<std::uint64_t, static_cast<std::size_t>(Counter::Count)>;

#if defined(PLATFORMATOR_EDITOR_WORK_COUNTERS)
inline std::array<std::atomic<std::uint64_t>, static_cast<std::size_t>(Counter::Count)> counters{};
inline constexpr bool enabled = true;

inline void record(Counter counter, std::uint64_t amount = 1) noexcept
{
    counters[static_cast<std::size_t>(counter)].fetch_add(amount, std::memory_order_relaxed);
}

inline Counts snapshot() noexcept
{
    Counts result{};
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = counters[i].load(std::memory_order_relaxed);
    return result;
}

inline void reset() noexcept
{
    for (auto &counter : counters)
        counter.store(0, std::memory_order_relaxed);
}
#else
inline constexpr bool enabled = false;
inline constexpr void record(Counter, std::uint64_t = 1) noexcept {}
inline constexpr Counts snapshot() noexcept { return {}; }
inline constexpr void reset() noexcept {}
#endif
} // namespace platformator_editor::work
