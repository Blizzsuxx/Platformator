#pragma once

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "sessionids.h"

namespace platformator_editor
{
enum class ProjectChangeKind
{
    Added,
    Modified,
    Removed,
    Renamed,
    Overflow,
    RootUnavailable,
    RootAvailable,
    NotificationsUnavailable
};

struct ProjectChangeEvent
{
    ProjectChangeKind kind = ProjectChangeKind::Modified;
    std::filesystem::path path;
    std::filesystem::path oldPath;
    std::vector<std::filesystem::path> affectedPaths;
    bool directory = false;
    ProjectGeneration projectGeneration;
    // Shared by every affected-path/alias delivery; coalescing a later write
    // advances the delivered event identity even when file metadata is unchanged.
    std::uint64_t sequence = 0;
};

enum class ProjectCoverageState
{
    Starting,
    Reconciling,
    Current,
    Degraded,
    RootUnavailable,
    Stopped
};

struct ProjectSnapshotToken
{
    ProjectGeneration projectGeneration;
    std::uint64_t requestId = 0;
    std::uint64_t startedAtSequence = 0;
    std::uint64_t uncertaintyEpoch = 0;
    bool recoveryRequired = false;
};

struct DirectoryListingToken
{
    ProjectGeneration projectGeneration;
    std::filesystem::path directory;
    std::uint64_t requestId = 0;
    std::uint64_t invalidationRevision = 0;
};

struct ProjectChangeBatch
{
    ProjectGeneration projectGeneration;
    std::vector<ProjectChangeEvent> events;
    bool rootDirty = false;
    std::uint64_t uncertaintyEpoch = 0;
    ProjectCoverageState coverage = ProjectCoverageState::Starting;
};

class RecoveryRetrySchedule
{
  public:
    using Clock = std::chrono::steady_clock;

    RecoveryRetrySchedule() noexcept : nextDue_(Clock::now()) {}

    void request_now(Clock::time_point now) noexcept
    {
        failures_ = 0;
        nextDue_ = now;
    }

    Clock::time_point retry_after_failure(Clock::time_point now) noexcept;
    void schedule_success(Clock::time_point now, Clock::duration interval) noexcept
    {
        failures_ = 0;
        nextDue_ = now + interval;
    }

    bool due(Clock::time_point now) const noexcept { return now >= nextDue_; }
    Clock::time_point deadline() const noexcept { return nextDue_; }
    std::size_t failures() const noexcept { return failures_; }

  private:
    Clock::time_point nextDue_{};
    std::size_t failures_ = 0;
};

class ProjectChanges
{
  public:
    static constexpr std::size_t MaxWatchedDirectories = 4096;
    static constexpr std::size_t MaxQueuedPaths = 8192;
    static constexpr std::size_t MaxQueuedBytes = 2U * 1024U * 1024U;

    explicit ProjectChanges(std::size_t maxQueuedPaths = MaxQueuedPaths,
                            std::size_t maxQueuedBytes = MaxQueuedBytes);

    void start(ProjectGeneration projectGeneration, std::filesystem::path projectRoot);
    void stop() noexcept;
    ProjectSnapshotToken begin_root_snapshot();
    bool complete_root_snapshot(const ProjectSnapshotToken &token, bool complete,
                                bool rootUnavailable = false);
    bool complete_root_snapshot(
        const ProjectSnapshotToken &token,
        const std::vector<std::pair<std::filesystem::path, std::string>> &inventory,
        bool complete, bool rootUnavailable = false);
    void set_notifications_available(bool available) noexcept;
    void submit(ProjectChangeEvent event,
                std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());
    ProjectChangeBatch take_batch(
        std::size_t maxEvents = 256,
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now(),
        bool force = false);
    std::optional<std::chrono::steady_clock::time_point> next_batch_deadline() const noexcept;

    DirectoryListingToken begin_directory_listing(const std::filesystem::path &directory);
    bool directory_listing_current(const DirectoryListingToken &token) const;
    void cancel_directory_listing(const DirectoryListingToken &token);
    bool complete_directory_listing(const DirectoryListingToken &token, bool complete,
                                    bool rootUnavailable = false);
    void invalidate_directory_revision(const std::filesystem::path &path, bool pathIsDirectory);
    void report_retry_failure(const std::filesystem::path &path);

    ProjectCoverageState coverage() const noexcept;
    ProjectGeneration project_generation() const noexcept;
    std::uint64_t uncertainty_epoch() const noexcept;
    std::uint64_t directory_revision(const std::filesystem::path &directory) const;
    std::size_t queued_paths() const noexcept;
    std::size_t queued_bytes() const noexcept;
    std::size_t retry_failures(const std::filesystem::path &path) const;

  private:
    struct PendingEvent
    {
        ProjectChangeEvent event;
        std::string key;
        std::size_t bytes = 0;
        std::uint64_t sequence = 0;
    };
    struct DirectoryRequest
    {
        std::uint64_t requestId = 0;
        std::uint64_t revision = 0;
    };

    std::optional<std::filesystem::path> normalized_path(const std::filesystem::path &path) const;
    std::string key_for(const std::filesystem::path &path) const;
    void invalidate_directory(const std::filesystem::path &path, bool pathIsDirectory);
    void mark_root_dirty(ProjectCoverageState state) noexcept;

    std::filesystem::path root_;
    ProjectGeneration projectGeneration_;
    ProjectCoverageState coverage_ = ProjectCoverageState::Stopped;
    std::map<std::string, PendingEvent> pending_;
    std::map<std::string, std::uint64_t> directoryRevisions_;
    std::map<std::string, DirectoryRequest> directoryRequests_;
    std::map<std::string, std::size_t> retryFailures_;
    std::map<std::string, std::string> acceptedInventory_;
    std::size_t maxQueuedPaths_;
    std::size_t maxQueuedBytes_;
    std::size_t queuedBytes_ = 0;
    std::uint64_t nextRequestId_ = 1;
    std::uint64_t sequence_ = 0;
    std::uint64_t uncertaintyEpoch_ = 0;
    std::uint64_t latestSnapshotRequest_ = 0;
    std::uint64_t inventoryBarrierSequence_ = 0;
    std::chrono::steady_clock::time_point eventBatchStartedAt_{};
    std::chrono::steady_clock::time_point lastEventAt_{};
    bool rootDirty_ = true;
    bool inventoryInitialized_ = false;
    bool inventoryBarrierPending_ = false;
    bool notificationsAvailable_ = true;
};
} // namespace platformator_editor
