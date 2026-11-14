#include "projectsession.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace platformator_editor
{
bool should_skip_project_directory(std::string_view name) noexcept
{
    return should_skip_directory(name);
}

namespace
{
std::size_t event_bytes(const ProjectChangeEvent &event)
{
    using NativeChar = std::filesystem::path::value_type;
    std::size_t bytes = sizeof(ProjectChangeEvent) +
                        (event.path.native().size() + event.oldPath.native().size()) * sizeof(NativeChar);
    for (const auto &path : event.affectedPaths)
        bytes += path.native().size() * sizeof(NativeChar);
    return bytes;
}

bool path_is_within(const std::filesystem::path &root, const std::filesystem::path &candidate)
{
    const auto relative = candidate.lexically_relative(root);
    if (relative.empty())
        return candidate == root;
    return *relative.begin() != ".." && !relative.is_absolute();
}

bool relevant_source_path(const std::filesystem::path &root, const std::filesystem::path &assetRoot,
                          const std::filesystem::path &path, bool directory)
{
    if (path.empty() || !path_is_within(root, path) ||
        (assetRoot != root && path_is_within(assetRoot, path)))
        return false;
    const auto relative = path.lexically_relative(root);
    for (const auto &part : relative)
        if (should_skip_project_directory(part.string()))
            return false;
    if (directory)
        return true;
    const auto filename = path.filename().string();
    const auto extension = path.extension().string();
    static const std::array<std::string_view, 10> sourceExtensions{
        ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".cmake", ".in"};
    return filename == "CMakeLists.txt" || filename == "CMakePresets.json" ||
           filename == "CMakeUserPresets.json" ||
           std::find(sourceExtensions.begin(), sourceExtensions.end(), extension) != sourceExtensions.end();
}

bool source_relevant_event(const ProjectSnapshot &snapshot, const ProjectChangeEvent &event)
{
    if (event.kind == ProjectChangeKind::Overflow)
        return event.path.empty() || relevant_source_path(snapshot.projectRoot, snapshot.assetRoot,
                                                          event.path, event.directory);
    if (event.kind == ProjectChangeKind::RootUnavailable ||
        event.kind == ProjectChangeKind::RootAvailable)
        return event.path.empty() || event.path == snapshot.projectRoot;
    if (event.kind == ProjectChangeKind::NotificationsUnavailable)
        return event.path.empty() || relevant_source_path(snapshot.projectRoot, snapshot.assetRoot,
                                                          event.path, event.directory);
    if (relevant_source_path(snapshot.projectRoot, snapshot.assetRoot, event.path, event.directory) ||
        relevant_source_path(snapshot.projectRoot, snapshot.assetRoot, event.oldPath, event.directory))
        return true;
    return std::any_of(event.affectedPaths.begin(), event.affectedPaths.end(), [&](const auto &path) {
        return relevant_source_path(snapshot.projectRoot, snapshot.assetRoot, path, event.directory);
    });
}

void advance_revision(std::atomic<std::uint64_t> &revision) noexcept
{
    auto current = revision.load(std::memory_order_relaxed);
    while (current != std::numeric_limits<std::uint64_t>::max() &&
           !revision.compare_exchange_weak(current, current + 1,
                                           std::memory_order_release,
                                           std::memory_order_relaxed))
    {
    }
}
} // namespace

ProjectSession::ProjectSession(std::filesystem::path projectRoot,
                               std::filesystem::path assetRoot)
    : documents_(projectRoot, assetRoot, ProjectGeneration{1})
{
    projectContext_.set_roots(projectRoot, assetRoot);
    start_monitoring(projectContext_.snapshot());
}

ProjectSession::~ProjectSession()
{
    stop_monitoring();
}

ProjectContext &ProjectSession::project_context() noexcept { return projectContext_; }
const ProjectContext &ProjectSession::project_context() const noexcept { return projectContext_; }
DocumentManager &ProjectSession::documents() noexcept { return documents_; }
const DocumentManager &ProjectSession::documents() const noexcept { return documents_; }

ProjectGeneration ProjectSession::generation() const noexcept
{
    return ProjectGeneration{projectContext_.snapshot().generation};
}

ProjectChanges &ProjectSession::project_changes() noexcept { return projectChanges_; }
ResourceIndex &ProjectSession::resource_index() noexcept { return resourceIndex_; }

ProjectChangeBatch ProjectSession::take_change_batch(std::size_t maxEvents)
{
    std::vector<ProjectChangeEvent> events;
    bool overflow = false;
    {
        std::lock_guard lock(inboxMutex_);
        overflow = std::exchange(inboxOverflow_, false);
        if (overflow)
        {
            inbox_.clear();
            inboxBytes_ = 0;
        }
        else
        {
            const std::size_t limit = std::min<std::size_t>(maxEvents, 256);
            events.reserve(std::min(inbox_.size(), limit));
            while (!inbox_.empty() && events.size() < limit)
            {
                const auto bytes = event_bytes(inbox_.front());
                inboxBytes_ -= std::min(inboxBytes_, bytes);
                events.push_back(std::move(inbox_.front()));
                inbox_.pop_front();
            }
        }
    }
    const auto processingStarted = std::chrono::steady_clock::now();
    std::size_t processed = 0;
    for (; processed < events.size(); ++processed)
    {
        auto &event = events[processed];
        if (event.kind == ProjectChangeKind::RootUnavailable)
            projectChanges_.set_notifications_available(false);
        else if (event.kind == ProjectChangeKind::RootAvailable)
            projectChanges_.set_notifications_available(true);
        projectChanges_.submit(std::move(event));
        if (std::chrono::steady_clock::now() - processingStarted >= std::chrono::milliseconds(2))
        {
            ++processed;
            break;
        }
    }
    if (processed < events.size())
    {
        std::lock_guard lock(inboxMutex_);
        std::size_t restoreBytes = 0;
        for (std::size_t index = processed; index < events.size(); ++index)
            restoreBytes += event_bytes(events[index]);
        if (inbox_.size() + events.size() - processed > ProjectChanges::MaxQueuedPaths ||
            restoreBytes > ProjectChanges::MaxQueuedBytes -
                std::min(inboxBytes_, ProjectChanges::MaxQueuedBytes))
        {
            inbox_.clear();
            inboxBytes_ = 0;
            inboxOverflow_ = true;
        }
        else
        {
            try
            {
                for (std::size_t index = events.size(); index > processed; --index)
                {
                    inboxBytes_ += event_bytes(events[index - 1]);
                    inbox_.push_front(std::move(events[index - 1]));
                }
            }
            catch (...)
            {
                inbox_.clear();
                inboxBytes_ = 0;
                inboxOverflow_ = true;
            }
        }
    }
    if (overflow)
    {
        ProjectChangeEvent event;
        event.kind = ProjectChangeKind::Overflow;
        event.projectGeneration = generation();
        projectChanges_.submit(std::move(event));
    }
    return projectChanges_.take_batch(maxEvents);
}

const std::string &ProjectSession::monitor_error() const noexcept { return monitorError_; }

std::uint64_t ProjectSession::source_change_revision() const noexcept
{
    return sourceChangeRevision_.load(std::memory_order_acquire);
}

bool ProjectSession::source_notifications_available() const noexcept
{
    return sourceNotificationsAvailable_.load(std::memory_order_acquire);
}

void ProjectSession::set_wake_callback(std::function<void()> callback)
{
    std::lock_guard lock(wakeMutex_);
    wakeCallback_ = std::move(callback);
}

WorkspaceActionResult ProjectSession::set_project_roots(
    const std::filesystem::path &projectRoot, const std::filesystem::path &assetRoot,
    bool discardDirtyDocuments)
{
    try
    {
        auto candidate = projectContext_.prepare_roots(projectRoot, assetRoot);
        if (candidate.generation == projectContext_.snapshot().generation)
            return {WorkspaceActionStatus::Completed, {}};

        const ProjectSnapshot previous = projectContext_.snapshot();
        stop_monitoring();
        const auto changed = documents_.set_project_roots(candidate.projectRoot, candidate.assetRoot,
                                                           ProjectGeneration{candidate.generation},
                                                           discardDirtyDocuments);
        if (!changed)
        {
            start_monitoring(previous);
            return changed;
        }
        projectContext_.commit_roots(std::move(candidate));
        start_monitoring(projectContext_.snapshot());
        return {WorkspaceActionStatus::Completed, {}};
    }
    catch (const std::exception &error)
    {
        return {WorkspaceActionStatus::Failed, error.what()};
    }
}

void ProjectSession::publish_source_change(const ProjectSnapshot &snapshot,
                                          const ProjectChangeEvent &event)
{
    if (!source_relevant_event(snapshot, event))
        return;
    if (event.kind == ProjectChangeKind::RootUnavailable ||
        event.kind == ProjectChangeKind::NotificationsUnavailable)
        sourceNotificationsAvailable_.store(false, std::memory_order_release);
    else if (event.kind == ProjectChangeKind::RootAvailable)
        sourceNotificationsAvailable_.store(true, std::memory_order_release);

    advance_revision(sourceChangeRevision_);
    std::function<void()> wake;
    {
        std::lock_guard lock(wakeMutex_);
        wake = wakeCallback_;
    }
    if (wake)
    {
        try
        {
            wake();
        }
        catch (...)
        {
        }
    }
}

void ProjectSession::queue_asset_change(ProjectChangeEvent event)
{
    const std::size_t bytes = event_bytes(event);
    {
        std::lock_guard lock(inboxMutex_);
        if (!inboxOverflow_ &&
            (inbox_.size() >= ProjectChanges::MaxQueuedPaths ||
             bytes > ProjectChanges::MaxQueuedBytes - std::min(inboxBytes_, ProjectChanges::MaxQueuedBytes)))
        {
            inbox_.clear();
            inboxBytes_ = 0;
            inboxOverflow_ = true;
        }
        else if (!inboxOverflow_)
        {
            try
            {
                inbox_.push_back(std::move(event));
                inboxBytes_ += bytes;
            }
            catch (...)
            {
                inbox_.clear();
                inboxBytes_ = 0;
                inboxOverflow_ = true;
            }
        }
    }
    std::function<void()> wake;
    {
        std::lock_guard lock(wakeMutex_);
        wake = wakeCallback_;
    }
    if (wake)
    {
        try
        {
            wake();
        }
        catch (...)
        {
        }
    }
}

void ProjectSession::start_monitoring(const ProjectSnapshot &snapshot)
{
    watchBudget_ = std::make_shared<ProjectWatchBudget>();
    projectChanges_.start(ProjectGeneration{snapshot.generation}, snapshot.assetRoot);
    resourceIndex_.set_project(ProjectGeneration{snapshot.generation}, snapshot.assetRoot);

    const bool assetUnderProject = path_is_within(snapshot.projectRoot, snapshot.assetRoot);
    const auto route_asset_event = [this, snapshot, assetUnderProject](
                                       const ProjectChangeEvent &event) {
        const auto is_asset_path = [&](const std::filesystem::path &path) {
            return !path.empty() && path_is_within(snapshot.assetRoot, path);
        };
        const auto asset_path_is_relevant = [&](const ProjectChangeEvent &candidate) {
            if ((candidate.kind == ProjectChangeKind::Overflow ||
                 candidate.kind == ProjectChangeKind::NotificationsUnavailable) &&
                candidate.path.empty())
                return true;
            if (is_asset_path(candidate.path) || is_asset_path(candidate.oldPath) ||
                std::any_of(candidate.affectedPaths.begin(), candidate.affectedPaths.end(), is_asset_path))
                return true;
            // Loss reported for an ancestor of the asset root also leaves the
            // nested asset tree uncertain, even if no asset path was named.
            return assetUnderProject && candidate.directory && !candidate.path.empty() &&
                   path_is_within(candidate.path, snapshot.assetRoot);
        };
        const auto make_asset_root_event = [&](ProjectChangeKind kind) {
            ProjectChangeEvent routed;
            routed.kind = kind;
            routed.path = snapshot.assetRoot;
            routed.directory = true;
            routed.projectGeneration = ProjectGeneration{snapshot.generation};
            return routed;
        };
        const auto asset_root_exists = [&] {
            std::error_code error;
            const auto status = std::filesystem::symlink_status(snapshot.assetRoot, error);
            return !error && !std::filesystem::is_symlink(status) &&
                   std::filesystem::is_directory(status);
        };
        if (event.kind == ProjectChangeKind::RootUnavailable &&
            event.path == snapshot.projectRoot)
        {
            if (assetUnderProject)
                queue_asset_change(make_asset_root_event(ProjectChangeKind::RootUnavailable));
            return;
        }
        if (event.kind == ProjectChangeKind::RootAvailable &&
            event.path == snapshot.projectRoot)
        {
            if (assetUnderProject && !asset_root_exists())
            {
                queue_asset_change(make_asset_root_event(ProjectChangeKind::RootUnavailable));
                return;
            }
            if (assetUnderProject)
            {
                queue_asset_change(make_asset_root_event(ProjectChangeKind::RootAvailable));
                queue_asset_change(make_asset_root_event(ProjectChangeKind::Overflow));
            }
            return;
        }
        if (event.kind == ProjectChangeKind::RootUnavailable &&
            event.path == snapshot.assetRoot)
        {
            queue_asset_change(make_asset_root_event(ProjectChangeKind::RootUnavailable));
            return;
        }
        if (event.kind == ProjectChangeKind::RootAvailable &&
            event.path == snapshot.assetRoot)
        {
            queue_asset_change(make_asset_root_event(ProjectChangeKind::RootAvailable));
            queue_asset_change(make_asset_root_event(ProjectChangeKind::Overflow));
            return;
        }
        if (event.kind == ProjectChangeKind::Overflow ||
            event.kind == ProjectChangeKind::NotificationsUnavailable)
        {
            if (!asset_path_is_relevant(event))
                return;
            ProjectChangeEvent routed = event;
            routed.path = snapshot.assetRoot;
            routed.oldPath.clear();
            routed.affectedPaths.clear();
            routed.directory = true;
            queue_asset_change(std::move(routed));
            return;
        }

        if (event.directory && event.kind == ProjectChangeKind::Removed &&
            event.path == snapshot.assetRoot)
        {
            queue_asset_change(make_asset_root_event(ProjectChangeKind::RootUnavailable));
            return;
        }
        if (event.directory && event.kind == ProjectChangeKind::Added &&
            event.path == snapshot.assetRoot)
        {
            queue_asset_change(make_asset_root_event(ProjectChangeKind::RootAvailable));
            queue_asset_change(make_asset_root_event(ProjectChangeKind::Overflow));
            return;
        }
        if (event.kind == ProjectChangeKind::Renamed)
        {
            if (event.oldPath == snapshot.assetRoot)
                queue_asset_change(make_asset_root_event(ProjectChangeKind::RootUnavailable));
            if (event.path == snapshot.assetRoot && asset_root_exists())
            {
                queue_asset_change(make_asset_root_event(ProjectChangeKind::RootAvailable));
                queue_asset_change(make_asset_root_event(ProjectChangeKind::Overflow));
                return;
            }
            if (event.oldPath == snapshot.assetRoot)
                return;
        }
        if (is_asset_path(event.path) || is_asset_path(event.oldPath) ||
            std::any_of(event.affectedPaths.begin(), event.affectedPaths.end(), is_asset_path))
            queue_asset_change(event);
    };

    const auto mark_asset_monitoring_unavailable = [&](const std::string &error) {
        projectChanges_.set_notifications_available(false);
        monitorError_ = error.empty()
            ? "Native asset monitoring is unavailable; refresh requires bounded fallback scans."
            : error;
        ProjectChangeEvent unavailable;
        unavailable.kind = ProjectChangeKind::NotificationsUnavailable;
        unavailable.path = snapshot.assetRoot;
        unavailable.projectGeneration = ProjectGeneration{snapshot.generation};
        projectChanges_.submit(std::move(unavailable));
    };

    // EFFICIENCY: one platform owner shares native notification resources and
    // the project-wide watch budget across source and asset roots. Nested assets
    // are included in the project-root watch; disjoint asset trees are supplied
    // as another root. Source freshness and asset invalidation remain routed
    // independently below.
    watcher_ = make_project_watch_service();
    if (!watcher_ || !watcher_->supported())
    {
        sourceNotificationsAvailable_.store(false, std::memory_order_release);
        mark_asset_monitoring_unavailable({});
        watcher_.reset();
        return;
    }
    std::string error;
    ProjectWatchOptions watchOptions;
    watchOptions.sourceInputsOnly = true;
    watchOptions.budget = watchBudget_;
    if (assetUnderProject)
        watchOptions.includedSubtrees.push_back(snapshot.assetRoot);
    else
        watchOptions.additionalRoots.push_back(snapshot.assetRoot);
    const bool started = watcher_->start(ProjectGeneration{snapshot.generation}, snapshot.projectRoot,
        [this, snapshot, route_asset_event](ProjectChangeEvent event) {
            publish_source_change(snapshot, event);
            route_asset_event(event);
        }, error, std::move(watchOptions));
    sourceNotificationsAvailable_.store(started, std::memory_order_release);
    projectChanges_.set_notifications_available(started);
    if (!started)
    {
        sourceNotificationsAvailable_.store(false, std::memory_order_release);
        mark_asset_monitoring_unavailable(error);
        watcher_.reset();
    }
    else
        monitorError_.clear();
}

void ProjectSession::stop_monitoring() noexcept
{
    sourceNotificationsAvailable_.store(false, std::memory_order_release);
    if (watcher_)
    {
        watcher_->stop();
        watcher_.reset();
    }
    watchBudget_.reset();
    projectChanges_.stop();
    std::lock_guard lock(inboxMutex_);
    inbox_.clear();
    inboxBytes_ = 0;
    inboxOverflow_ = false;
}

} // namespace platformator_editor
