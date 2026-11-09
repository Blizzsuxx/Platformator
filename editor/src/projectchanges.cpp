#include "projectchanges.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace platformator_editor
{
RecoveryRetrySchedule::Clock::time_point RecoveryRetrySchedule::retry_after_failure(
    Clock::time_point now) noexcept
{
    failures_ = std::min<std::size_t>(failures_ + 1, 9);
    const auto multiplier = 1U << std::min<std::size_t>(failures_ - 1, 8);
    const auto delay = std::min(std::chrono::milliseconds(30000),
                                std::chrono::milliseconds(200) * multiplier);
    nextDue_ = now + delay;
    return nextDue_;
}

ProjectChanges::ProjectChanges(std::size_t maxQueuedPaths, std::size_t maxQueuedBytes)
    : maxQueuedPaths_(std::min(maxQueuedPaths, MaxQueuedPaths)),
      maxQueuedBytes_(std::min(maxQueuedBytes, MaxQueuedBytes))
{
}

void ProjectChanges::start(ProjectGeneration projectGeneration, std::filesystem::path projectRoot)
{
    root_ = std::move(projectRoot).lexically_normal();
    projectGeneration_ = projectGeneration;
    pending_.clear();
    directoryRevisions_.clear();
    directoryRequests_.clear();
    retryFailures_.clear();
    acceptedInventory_.clear();
    queuedBytes_ = 0;
    sequence_ = 0;
    latestSnapshotRequest_ = 0;
    inventoryBarrierSequence_ = 0;
    inventoryInitialized_ = false;
    inventoryBarrierPending_ = false;
    eventBatchStartedAt_ = {};
    lastEventAt_ = {};
    rootDirty_ = true;
    notificationsAvailable_ = true;
    if (uncertaintyEpoch_ != std::numeric_limits<std::uint64_t>::max())
        ++uncertaintyEpoch_;
    coverage_ = ProjectCoverageState::Starting;
}

void ProjectChanges::stop() noexcept
{
    pending_.clear();
    directoryRevisions_.clear();
    directoryRequests_.clear();
    retryFailures_.clear();
    acceptedInventory_.clear();
    queuedBytes_ = 0;
    eventBatchStartedAt_ = {};
    lastEventAt_ = {};
    rootDirty_ = false;
    inventoryInitialized_ = false;
    inventoryBarrierPending_ = false;
    inventoryBarrierSequence_ = 0;
    coverage_ = ProjectCoverageState::Stopped;
}

ProjectSnapshotToken ProjectChanges::begin_root_snapshot()
{
    if (coverage_ == ProjectCoverageState::Stopped || nextRequestId_ == 0)
        return {projectGeneration_, 0, sequence_, uncertaintyEpoch_, rootDirty_};
    if (nextRequestId_ == std::numeric_limits<std::uint64_t>::max())
        return {projectGeneration_, 0, sequence_, uncertaintyEpoch_, rootDirty_};
    latestSnapshotRequest_ = nextRequestId_++;
    coverage_ = ProjectCoverageState::Reconciling;
    return {projectGeneration_, latestSnapshotRequest_, sequence_, uncertaintyEpoch_, rootDirty_};
}

bool ProjectChanges::complete_root_snapshot(const ProjectSnapshotToken &token, bool complete,
                                            bool rootUnavailable)
{
    if (token.requestId == 0 || token.projectGeneration != projectGeneration_ ||
        token.requestId != latestSnapshotRequest_)
        return false;
    if (!complete)
    {
        mark_root_dirty(rootUnavailable ? ProjectCoverageState::RootUnavailable
                                        : ProjectCoverageState::Degraded);
        return false;
    }
    if (token.uncertaintyEpoch != uncertaintyEpoch_)
    {
        mark_root_dirty(ProjectCoverageState::Reconciling);
        return false;
    }
    for (auto item = pending_.begin(); item != pending_.end();)
    {
        if (item->second.sequence <= token.startedAtSequence)
        {
            queuedBytes_ -= item->second.bytes;
            item = pending_.erase(item);
        }
        else
            ++item;
    }
    rootDirty_ = false;
    coverage_ = notificationsAvailable_ ? ProjectCoverageState::Current : ProjectCoverageState::Degraded;
    return true;
}

bool ProjectChanges::complete_root_snapshot(
    const ProjectSnapshotToken &token,
    const std::vector<std::pair<std::filesystem::path, std::string>> &inventory,
    bool complete, bool rootUnavailable)
{
    if (token.requestId == 0 || token.projectGeneration != projectGeneration_ ||
        token.requestId != latestSnapshotRequest_)
        return false;
    if (rootUnavailable)
        return complete_root_snapshot(token, false, true);
    if (!complete)
        return complete_root_snapshot(token, false, rootUnavailable);

    std::map<std::string, std::string> nextInventory;
    for (const auto &[path, identity] : inventory)
    {
        const auto normalized = normalized_path(path);
        if (!normalized || *normalized == root_)
        {
            mark_root_dirty(ProjectCoverageState::Degraded);
            return false;
        }
        nextInventory[key_for(*normalized)] = identity;
    }

    const bool forceSubtreeReconciliation = token.recoveryRequired || !inventoryInitialized_;
    const bool inventoryChanged = forceSubtreeReconciliation || nextInventory != acceptedInventory_;
    const auto previousInventory = acceptedInventory_;
    if (!complete_root_snapshot(token, true, rootUnavailable))
        return false;
    acceptedInventory_ = std::move(nextInventory);
    inventoryInitialized_ = true;
    if (!inventoryChanged)
        return true;

    if (forceSubtreeReconciliation)
    {
        ProjectChangeEvent reconciliation;
        reconciliation.kind = ProjectChangeKind::Modified;
        reconciliation.path = root_;
        reconciliation.affectedPaths.push_back(root_);
        reconciliation.directory = true;
        reconciliation.projectGeneration = projectGeneration_;
        submit(std::move(reconciliation));
    }
    else
    {
        std::map<std::string, ProjectChangeEvent> differences;
        for (const auto &[key, identity] : acceptedInventory_)
        {
            const auto previous = previousInventory.find(key);
            if (previous != previousInventory.end() && previous->second == identity)
                continue;
            const auto path = root_ / std::filesystem::path(key);
            ProjectChangeEvent change;
            change.kind = previous == previousInventory.end() ? ProjectChangeKind::Added
                                                               : ProjectChangeKind::Modified;
            change.path = path;
            change.directory = identity.starts_with("d:") || identity.starts_with("l:") ||
                (previous != previousInventory.end() && previous->second.starts_with("d:"));
            change.affectedPaths.push_back(path);
            change.projectGeneration = projectGeneration_;
            differences.emplace(key, std::move(change));
        }
        for (const auto &[key, identity] : previousInventory)
        {
            if (acceptedInventory_.contains(key))
                continue;
            const auto path = root_ / std::filesystem::path(key);
            ProjectChangeEvent change;
            change.kind = ProjectChangeKind::Removed;
            change.path = path;
            change.directory = identity.starts_with("d:") || identity.starts_with("l:");
            change.affectedPaths.push_back(path);
            change.projectGeneration = projectGeneration_;
            differences.emplace(key, std::move(change));
        }
        for (auto &[key, change] : differences)
        {
            (void)key;
            submit(std::move(change));
            if (rootDirty_)
                return false;
        }
    }
    if (rootDirty_)
        return false;
    if (pending_.empty())
    {
        inventoryBarrierSequence_ = 0;
        inventoryBarrierPending_ = false;
        coverage_ = notificationsAvailable_ ? ProjectCoverageState::Current
                                            : ProjectCoverageState::Degraded;
        return true;
    }
    // The newest replay/diff event is the barrier. Earlier events are drained first,
    // including events observed while this snapshot was in flight.
    inventoryBarrierSequence_ = sequence_;
    inventoryBarrierPending_ = true;
    coverage_ = ProjectCoverageState::Reconciling;
    return true;
}

void ProjectChanges::set_notifications_available(bool available) noexcept
{
    notificationsAvailable_ = available;
    if (!available && coverage_ == ProjectCoverageState::Current)
        coverage_ = ProjectCoverageState::Degraded;
}

void ProjectChanges::submit(ProjectChangeEvent event, std::chrono::steady_clock::time_point now)
{
    if (coverage_ == ProjectCoverageState::Stopped || event.projectGeneration != projectGeneration_)
        return;
    if (event.kind == ProjectChangeKind::Overflow)
    {
        mark_root_dirty(ProjectCoverageState::Degraded);
        return;
    }
    if (event.kind == ProjectChangeKind::RootUnavailable)
    {
        mark_root_dirty(ProjectCoverageState::RootUnavailable);
        return;
    }
    if (event.kind == ProjectChangeKind::RootAvailable)
    {
        notificationsAvailable_ = true;
        if (coverage_ == ProjectCoverageState::RootUnavailable)
        {
            if (uncertaintyEpoch_ != std::numeric_limits<std::uint64_t>::max())
                ++uncertaintyEpoch_;
            rootDirty_ = true;
            coverage_ = ProjectCoverageState::Reconciling;
        }
        return;
    }
    if (event.kind == ProjectChangeKind::NotificationsUnavailable)
    {
        notificationsAvailable_ = false;
        mark_root_dirty(ProjectCoverageState::Degraded);
        return;
    }
    const auto path = normalized_path(event.path);
    if (!path)
    {
        mark_root_dirty(ProjectCoverageState::Degraded);
        return;
    }
    event.path = *path;
    if (event.kind == ProjectChangeKind::Renamed)
    {
        const auto oldPath = normalized_path(event.oldPath);
        if (!oldPath)
        {
            mark_root_dirty(ProjectCoverageState::Degraded);
            return;
        }
        event.oldPath = *oldPath;
        invalidate_directory(event.oldPath, event.directory);
    }

    for (auto &affectedPath : event.affectedPaths)
    {
        const auto normalizedAffected = normalized_path(affectedPath);
        if (!normalizedAffected)
        {
            mark_root_dirty(ProjectCoverageState::Degraded);
            return;
        }
        affectedPath = *normalizedAffected;
    }
    const auto append_affected_path = [](ProjectChangeEvent &target,
                                         const std::filesystem::path &path) {
        if (path.empty() || std::find(target.affectedPaths.begin(), target.affectedPaths.end(), path) !=
                                target.affectedPaths.end())
            return;
        target.affectedPaths.push_back(path);
    };
    append_affected_path(event, event.path);
    append_affected_path(event, event.oldPath);

    if (sequence_ == std::numeric_limits<std::uint64_t>::max())
    {
        mark_root_dirty(ProjectCoverageState::Degraded);
        return;
    }
    ++sequence_;
    event.sequence = sequence_;
    if (pending_.empty())
        eventBatchStartedAt_ = now;
    lastEventAt_ = now;
    invalidate_directory(event.path, event.directory);
    const std::string key = key_for(event.path);
    const auto event_bytes = [](const ProjectChangeEvent &candidate) {
        std::size_t bytes = candidate.path.generic_string().size() +
                            candidate.oldPath.generic_string().size() + sizeof(ProjectChangeEvent);
        for (const auto &path : candidate.affectedPaths)
            bytes += path.generic_string().size();
        return bytes;
    };
    const auto merge_affected_paths = [&](ProjectChangeEvent &target,
                                          const ProjectChangeEvent &source) {
        for (const auto &path : source.affectedPaths)
            append_affected_path(target, path);
    };

    // Fold a chain A→B→C into one A→C event while retaining B for consumers
    // and listings that still hold an intermediate logical path.
    if (event.kind == ProjectChangeKind::Renamed)
    {
        const auto previousRename = std::find_if(pending_.begin(), pending_.end(), [&](const auto &item) {
            return item.second.event.kind == ProjectChangeKind::Renamed &&
                   item.second.event.path == event.oldPath;
        });
        if (previousRename != pending_.end())
        {
            const auto originalPath = previousRename->second.event.oldPath;
            merge_affected_paths(event, previousRename->second.event);
            event.oldPath = originalPath;
            queuedBytes_ -= previousRename->second.bytes;
            pending_.erase(previousRename);
        }
    }

    const std::size_t bytes = event_bytes(event);
    auto found = pending_.find(key);
    if (found != pending_.end())
    {
        auto &previous = found->second;
        const auto before = previous.bytes;
        if (previous.event.kind == ProjectChangeKind::Added && event.kind == ProjectChangeKind::Modified)
        {
            previous.event.directory = previous.event.directory || event.directory;
            merge_affected_paths(previous.event, event);
            previous.sequence = sequence_;
            previous.bytes = event_bytes(previous.event);
        }
        else if (previous.event.kind == ProjectChangeKind::Added && event.kind == ProjectChangeKind::Removed)
        {
            queuedBytes_ -= previous.bytes;
            pending_.erase(found);
            if (pending_.empty())
            {
                eventBatchStartedAt_ = {};
                lastEventAt_ = {};
            }
            return;
        }
        else if (previous.event.kind == ProjectChangeKind::Modified && event.kind == ProjectChangeKind::Removed)
        {
            const bool directory = previous.event.directory || event.directory;
            auto affectedPaths = previous.event;
            previous.event = event;
            merge_affected_paths(previous.event, affectedPaths);
            previous.event.directory = directory;
            previous.bytes = event_bytes(previous.event);
            previous.sequence = sequence_;
        }
        else if (previous.event.kind == ProjectChangeKind::Removed && event.kind == ProjectChangeKind::Added)
        {
            const bool directory = previous.event.directory || event.directory;
            auto affectedPaths = previous.event;
            previous.event = event;
            previous.event.kind = ProjectChangeKind::Modified;
            merge_affected_paths(previous.event, affectedPaths);
            previous.event.directory = directory;
            previous.bytes = event_bytes(previous.event);
            previous.sequence = sequence_;
        }
        else if (previous.event.kind == ProjectChangeKind::Renamed &&
                 previous.event.path == event.path &&
                 (event.kind == ProjectChangeKind::Modified || event.kind == ProjectChangeKind::Added))
        {
            previous.event.directory = previous.event.directory || event.directory;
            merge_affected_paths(previous.event, event);
            previous.bytes = event_bytes(previous.event);
            previous.sequence = sequence_;
        }
        else
        {
            const bool directory = previous.event.directory || event.directory;
            auto affectedPaths = previous.event;
            previous.event = event;
            merge_affected_paths(previous.event, affectedPaths);
            previous.event.directory = directory;
            previous.bytes = event_bytes(previous.event);
            previous.sequence = sequence_;
        }
        previous.event.sequence = sequence_;
        queuedBytes_ = queuedBytes_ - before + previous.bytes;
    }
    else
    {
        pending_.emplace(key, PendingEvent{std::move(event), key, bytes, sequence_});
        queuedBytes_ += bytes;
    }
    if (pending_.size() > maxQueuedPaths_ || queuedBytes_ > maxQueuedBytes_)
        mark_root_dirty(ProjectCoverageState::Degraded);
}

ProjectChangeBatch ProjectChanges::take_batch(std::size_t maxEvents,
                                              std::chrono::steady_clock::time_point now,
                                              bool force)
{
    ProjectChangeBatch result;
    result.projectGeneration = projectGeneration_;
    result.rootDirty = rootDirty_;
    result.uncertaintyEpoch = uncertaintyEpoch_;
    result.coverage = coverage_;
    if (maxEvents == 0)
        return result;
    if (!force && !pending_.empty() && !rootDirty_ &&
        now - lastEventAt_ < std::chrono::milliseconds(200) &&
        now - eventBatchStartedAt_ < std::chrono::seconds(1))
        return result;
    auto item = pending_.begin();
    bool reconciliationBarrierDrained = false;
    while (item != pending_.end() && result.events.size() < maxEvents)
    {
        if (inventoryBarrierPending_ && item->second.sequence == inventoryBarrierSequence_)
        {
            const bool earlierEventPending = std::any_of(pending_.begin(), pending_.end(), [&](const auto &candidate) {
                return candidate.second.sequence < inventoryBarrierSequence_;
            });
            if (earlierEventPending)
            {
                ++item;
                continue;
            }
            reconciliationBarrierDrained = true;
        }
        queuedBytes_ -= item->second.bytes;
        result.events.push_back(std::move(item->second.event));
        item = pending_.erase(item);
    }
    if (pending_.empty())
    {
        eventBatchStartedAt_ = {};
        lastEventAt_ = {};
    }
    if (reconciliationBarrierDrained)
    {
        inventoryBarrierPending_ = false;
        inventoryBarrierSequence_ = 0;
        coverage_ = notificationsAvailable_ ? ProjectCoverageState::Current
                                            : ProjectCoverageState::Degraded;
    }
    return result;
}

std::optional<std::chrono::steady_clock::time_point>
ProjectChanges::next_batch_deadline() const noexcept
{
    if (pending_.empty())
        return std::nullopt;
    return std::min(lastEventAt_ + std::chrono::milliseconds(200),
                    eventBatchStartedAt_ + std::chrono::seconds(1));
}

DirectoryListingToken ProjectChanges::begin_directory_listing(const std::filesystem::path &directory)
{
    const auto normalized = normalized_path(directory);
    if (!normalized || coverage_ == ProjectCoverageState::Stopped || nextRequestId_ == 0 ||
        nextRequestId_ == std::numeric_limits<std::uint64_t>::max())
        return {projectGeneration_, {}, 0, 0};
    const auto key = key_for(*normalized);
    const std::uint64_t requestId = nextRequestId_++;
    const std::uint64_t revision = directoryRevisions_[key];
    directoryRequests_[key] = DirectoryRequest{requestId, revision};
    return {projectGeneration_, *normalized, requestId, revision};
}

void ProjectChanges::cancel_directory_listing(const DirectoryListingToken &token)
{
    if (token.requestId == 0 || token.projectGeneration != projectGeneration_)
        return;
    const auto request = directoryRequests_.find(key_for(token.directory));
    if (request != directoryRequests_.end() && request->second.requestId == token.requestId)
        directoryRequests_.erase(request);
}

bool ProjectChanges::directory_listing_current(const DirectoryListingToken &token) const
{
    if (token.requestId == 0 || token.projectGeneration != projectGeneration_)
        return false;
    const auto request = directoryRequests_.find(key_for(token.directory));
    return request != directoryRequests_.end() && request->second.requestId == token.requestId &&
           request->second.revision == token.invalidationRevision &&
           directory_revision(token.directory) == token.invalidationRevision;
}

bool ProjectChanges::complete_directory_listing(const DirectoryListingToken &token, bool complete,
                                                bool rootUnavailable)
{
    if (!directory_listing_current(token))
        return false;
    const auto key = key_for(token.directory);
    const auto request = directoryRequests_.find(key);
    directoryRequests_.erase(request);
    if (!complete)
    {
        if (rootUnavailable)
            mark_root_dirty(ProjectCoverageState::RootUnavailable);
        else if (coverage_ == ProjectCoverageState::Current)
            coverage_ = ProjectCoverageState::Degraded;
        return false;
    }
    retryFailures_.erase(key);
    return true;
}

void ProjectChanges::invalidate_directory_revision(const std::filesystem::path &path,
                                                   bool pathIsDirectory)
{
    invalidate_directory(path, pathIsDirectory);
}

void ProjectChanges::report_retry_failure(const std::filesystem::path &path)
{
    const auto normalized = normalized_path(path);
    if (!normalized)
    {
        mark_root_dirty(ProjectCoverageState::Degraded);
        return;
    }
    auto &failures = retryFailures_[key_for(*normalized)];
    if (failures != std::numeric_limits<std::size_t>::max())
        ++failures;
    if (coverage_ == ProjectCoverageState::Current)
        coverage_ = ProjectCoverageState::Degraded;
}

ProjectCoverageState ProjectChanges::coverage() const noexcept { return coverage_; }
ProjectGeneration ProjectChanges::project_generation() const noexcept { return projectGeneration_; }
std::uint64_t ProjectChanges::uncertainty_epoch() const noexcept { return uncertaintyEpoch_; }

std::uint64_t ProjectChanges::directory_revision(const std::filesystem::path &directory) const
{
    const auto normalized = normalized_path(directory);
    if (!normalized)
        return 0;
    const auto found = directoryRevisions_.find(key_for(*normalized));
    return found == directoryRevisions_.end() ? 0 : found->second;
}

std::size_t ProjectChanges::queued_paths() const noexcept { return pending_.size(); }
std::size_t ProjectChanges::queued_bytes() const noexcept { return queuedBytes_; }

std::size_t ProjectChanges::retry_failures(const std::filesystem::path &path) const
{
    const auto normalized = normalized_path(path);
    if (!normalized)
        return 0;
    const auto found = retryFailures_.find(key_for(*normalized));
    return found == retryFailures_.end() ? 0 : found->second;
}

std::optional<std::filesystem::path> ProjectChanges::normalized_path(const std::filesystem::path &path) const
{
    if (root_.empty() || path.empty())
        return std::nullopt;
    const auto absolute = (path.is_absolute() ? path : root_ / path).lexically_normal();
    const auto relative = absolute.lexically_relative(root_);
    if (relative.empty() && absolute != root_)
        return std::nullopt;
    for (const auto &part : relative)
        if (part == "..")
            return std::nullopt;
    return absolute;
}

std::string ProjectChanges::key_for(const std::filesystem::path &path) const
{
    if (path == root_)
        return {};
    return path.lexically_relative(root_).generic_string();
}

void ProjectChanges::invalidate_directory(const std::filesystem::path &path, bool pathIsDirectory)
{
    const auto increment = [&](std::uint64_t &revision) {
        if (revision == std::numeric_limits<std::uint64_t>::max())
        {
            mark_root_dirty(ProjectCoverageState::Degraded);
            return;
        }
        ++revision;
    };
    increment(directoryRevisions_[key_for(path.parent_path())]);
    if (pathIsDirectory)
    {
        const std::string subtree = key_for(path);
        directoryRevisions_.try_emplace(subtree, 0);
        for (auto &[directory, revision] : directoryRevisions_)
        {
            const bool contained = subtree.empty() || directory == subtree ||
                (directory.size() > subtree.size() && directory.starts_with(subtree) &&
                 directory[subtree.size()] == '/');
            if (contained)
                increment(revision);
        }
    }
}

void ProjectChanges::mark_root_dirty(ProjectCoverageState state) noexcept
{
    if (uncertaintyEpoch_ != std::numeric_limits<std::uint64_t>::max())
        ++uncertaintyEpoch_;
    rootDirty_ = true;
    pending_.clear();
    queuedBytes_ = 0;
    inventoryBarrierPending_ = false;
    inventoryBarrierSequence_ = 0;
    directoryRevisions_.try_emplace(key_for(root_), 0);
    for (auto &[directory, revision] : directoryRevisions_)
    {
        (void)directory;
        if (revision != std::numeric_limits<std::uint64_t>::max())
            ++revision;
    }
    eventBatchStartedAt_ = {};
    lastEventAt_ = {};
    if (state != ProjectCoverageState::Reconciling)
        coverage_ = state;
}
} // namespace platformator_editor
