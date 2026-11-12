#include "watch.h"

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <poll.h>
#include <sys/inotify.h>
#include <thread>
#include <unordered_map>
#include <unistd.h>
#include <utility>
#include <vector>

namespace platformator_editor
{
bool should_skip_watched_directory(std::string_view name) noexcept
{
    if (name.empty() || name == "." || name == "..")
        return false;
    if (name.starts_with('.'))
        return true;
    return name == "bin" || name == "build" || name == "dist" ||
           name == "vendored" || name == "vendor" || name == "node_modules" ||
           name == "specs" || name == "benchmark" || name == "diplomski" ||
           name == "paper" || name == "docs";
}

namespace
{
class LinuxProjectWatchService final : public ProjectWatchService
{
  public:
    ~LinuxProjectWatchService() override { stop(); }

    bool start(ProjectGeneration generation, const std::filesystem::path &projectRoot,
               Callback callback, std::string &error, ProjectWatchOptions options) override
    {
        stop();
        roots_.clear();
        const auto add_root = [&](const std::filesystem::path &requestedPath) {
            std::error_code filesystemError;
            const auto requestedRoot = std::filesystem::absolute(requestedPath, filesystemError).lexically_normal();
            if (filesystemError)
            {
                error = filesystemError.message();
                return false;
            }
            auto resolvedRoot = std::filesystem::canonical(requestedRoot, filesystemError);
            std::filesystem::path missingParentRecoveryAnchor;
            std::filesystem::path missingParentRecoveryName;
            if (filesystemError == std::errc::no_such_file_or_directory)
            {
                filesystemError.clear();
                const auto requestedParent = requestedRoot.parent_path();
                const auto canonicalParent = std::filesystem::canonical(requestedParent, filesystemError);
                if (!filesystemError && std::filesystem::is_directory(canonicalParent, filesystemError) &&
                    !filesystemError)
                    resolvedRoot = canonicalParent / requestedRoot.filename();
                else if (filesystemError == std::errc::no_such_file_or_directory)
                {
                    filesystemError.clear();
                    const auto requestedGrandparent = requestedParent.parent_path();
                    const auto canonicalGrandparent =
                        std::filesystem::canonical(requestedGrandparent, filesystemError);
                    if (!filesystemError &&
                        std::filesystem::is_directory(canonicalGrandparent, filesystemError) && !filesystemError)
                    {
                        // Keep the requested lexical paths so the recovery event
                        // can re-arm this root when its missing immediate parent
                        // is created beneath the nearest existing ancestor.
                        resolvedRoot = requestedRoot;
                        missingParentRecoveryAnchor = canonicalGrandparent;
                        missingParentRecoveryName = requestedParent.filename();
                    }
                }
            }
            if (filesystemError || resolvedRoot.empty())
            {
                error = filesystemError ? filesystemError.message() : "watch root is not a directory";
                return false;
            }
            filesystemError.clear();
            const bool rootIsDirectory = std::filesystem::is_directory(resolvedRoot, filesystemError);
            if (filesystemError && filesystemError != std::errc::no_such_file_or_directory)
            {
                error = filesystemError.message();
                return false;
            }
            if (!rootIsDirectory && !filesystemError)
            {
                error = "watch root is not a directory";
                return false;
            }
            if (std::none_of(roots_.begin(), roots_.end(), [&](const RootWatch &root) {
                    return root.path == resolvedRoot;
                }))
            {
                const auto parent = missingParentRecoveryAnchor.empty()
                    ? resolvedRoot.parent_path() : requestedRoot.parent_path();
                const auto recoveryParent = missingParentRecoveryAnchor.empty()
                    ? parent.parent_path() : missingParentRecoveryAnchor;
                const auto recoveryName = missingParentRecoveryName.empty()
                    ? parent.filename() : missingParentRecoveryName;
                roots_.push_back(RootWatch{resolvedRoot, parent, resolvedRoot.filename(), -1,
                                           recoveryParent == parent ? std::filesystem::path{} : recoveryParent,
                                           recoveryName, -1});
            }
            return true;
        };
        if (!add_root(projectRoot))
        {
            roots_.clear();
            return false;
        }
        for (const auto &additionalRoot : options.additionalRoots)
            if (!add_root(additionalRoot))
            {
                roots_.clear();
                return false;
            }
        root_ = roots_.front().path;
        generation_ = generation;
        callback_ = std::move(callback);
        sourceInputsOnly_ = options.sourceInputsOnly;
        budget_ = std::move(options.budget);
        includedSubtrees_ = std::move(options.includedSubtrees);
        stopping_.store(false, std::memory_order_release);
        parentRootsByWatch_.clear();
        recoveryRootsByWatch_.clear();
        try
        {
            worker_ = std::thread([this] { run(); });
        }
        catch (const std::exception &exception)
        {
            error = std::string("cannot start project watch worker: ") + exception.what();
            stop();
            return false;
        }
        return true;
    }

    void stop() noexcept override
    {
        stopping_.store(true, std::memory_order_release);
        if (worker_.joinable())
            worker_.join();
        callback_ = {};
        includedSubtrees_.clear();
        roots_.clear();
        parentRootsByWatch_.clear();
        recoveryRootsByWatch_.clear();
        root_.clear();
    }

    std::size_t watched_directories() const noexcept override
    {
        return reportedWatchCount_.load(std::memory_order_acquire);
    }
    bool supported() const noexcept override { return true; }

  private:
    struct RootWatch
    {
        std::filesystem::path path;
        std::filesystem::path parent;
        std::filesystem::path name;
        int parentDescriptor = -1;
        std::filesystem::path recoveryParent;
        std::filesystem::path recoveryName;
        int recoveryDescriptor = -1;
    };

    struct PendingMove
    {
        std::filesystem::path path;
        bool directory = false;
        std::chrono::steady_clock::time_point time;
    };

    bool skipped_source_directory(const std::filesystem::path &path) const
    {
        for (std::size_t index = 1; index < roots_.size(); ++index)
            if (contains_path(roots_[index].path, path))
                return false;
        for (const auto &included : includedSubtrees_)
            if (contains_path(included, path))
                return false;
        const auto relative = path.lexically_relative(root_);
        for (const auto &part : relative)
        {
            const auto name = part.string();
            if (should_skip_watched_directory(name))
                return true;
            if (sourceInputsOnly_ && name == "assets")
                return true;
        }
        return false;
    }

    int add_watch(const std::filesystem::path &directory, bool parent)
    {
        const std::string key = directory.lexically_normal().generic_string();
        const auto existing = watchesByPath_.find(key);
        if (existing != watchesByPath_.end())
            return existing->second;
        if (activeWatchCount_ >= ProjectWatchDirectoryLimit)
        {
            errno = ENOSPC;
            return -1;
        }
        if (budget_ && !budget_->try_acquire())
        {
            errno = ENOSPC;
            return -1;
        }
        const std::uint32_t mask = IN_CREATE | IN_DELETE | IN_CLOSE_WRITE | IN_MODIFY | IN_ATTRIB |
                                   IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF |
                                   IN_IGNORED | IN_ONLYDIR;
        const int descriptor = inotify_add_watch(fd_, directory.c_str(), mask);
        if (descriptor < 0)
        {
            if (budget_)
                budget_->release();
            return -1;
        }
        pathsByWatch_[descriptor] = directory.lexically_normal();
        watchesByPath_[key] = descriptor;
        ++activeWatchCount_;
        reportedWatchCount_.store(activeWatchCount_, std::memory_order_release);
        (void)parent;
        return descriptor;
    }

    bool add_tree(const std::filesystem::path &start)
    {
        constexpr std::size_t maximumEntries = 100000;
        constexpr std::size_t maximumPendingDirectories = ProjectWatchDirectoryLimit;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        std::vector<std::filesystem::path> pending{start};
        std::size_t visitedEntries = 0;
        while (!pending.empty())
        {
            if (stopping_.load(std::memory_order_acquire) ||
                std::chrono::steady_clock::now() >= deadline)
                return false;
            auto directory = std::move(pending.back());
            pending.pop_back();
            if (skipped_source_directory(directory))
                continue;
            std::error_code error;
            const auto status = std::filesystem::symlink_status(directory, error);
            if (error == std::errc::no_such_file_or_directory)
                continue;
            if (error || std::filesystem::is_symlink(status) || !std::filesystem::is_directory(status))
                return !error;
            if (add_watch(directory, false) < 0)
                return false;
            std::filesystem::directory_iterator current(directory, std::filesystem::directory_options::none, error);
            const std::filesystem::directory_iterator end;
            if (error)
                return false;
            while (!error && current != end)
            {
                if (stopping_.load(std::memory_order_acquire) ||
                    ++visitedEntries > maximumEntries ||
                    std::chrono::steady_clock::now() >= deadline)
                    return false;
                std::error_code entryError;
                const auto entryStatus = current->symlink_status(entryError);
                if (entryError)
                    return false;
                if (!std::filesystem::is_symlink(entryStatus) &&
                    std::filesystem::is_directory(entryStatus))
                {
                    if (!skipped_source_directory(current->path()))
                    {
                        // A very wide tree can otherwise retain tens of thousands
                        // of full paths before the first child watch is attempted.
                        // Such a tree cannot be covered by this service's directory
                        // budget, so stop discovery and publish degraded coverage.
                        if (pending.size() >= maximumPendingDirectories)
                        {
                            errno = ENOSPC;
                            return false;
                        }
                        pending.push_back(current->path());
                    }
                }
                current.increment(error);
            }
            if (error)
                return false;
        }
        return true;
    }

    void remove_watch(int descriptor)
    {
        const auto found = pathsByWatch_.find(descriptor);
        if (found == pathsByWatch_.end())
            return;
        watchesByPath_.erase(found->second.generic_string());
        pathsByWatch_.erase(found);
        if (const auto parents = parentRootsByWatch_.find(descriptor);
            parents != parentRootsByWatch_.end())
        {
            for (const auto rootIndex : parents->second)
                if (rootIndex < roots_.size() && roots_[rootIndex].parentDescriptor == descriptor)
                    roots_[rootIndex].parentDescriptor = -1;
            parentRootsByWatch_.erase(parents);
        }
        if (const auto recoveries = recoveryRootsByWatch_.find(descriptor);
            recoveries != recoveryRootsByWatch_.end())
        {
            for (const auto rootIndex : recoveries->second)
                if (rootIndex < roots_.size() && roots_[rootIndex].recoveryDescriptor == descriptor)
                    roots_[rootIndex].recoveryDescriptor = -1;
            recoveryRootsByWatch_.erase(recoveries);
        }
        if (activeWatchCount_ > 0)
            --activeWatchCount_;
        if (budget_)
            budget_->release();
        reportedWatchCount_.store(activeWatchCount_, std::memory_order_release);
        inotify_rm_watch(fd_, descriptor);
    }

    void register_parent_watch(std::size_t rootIndex)
    {
        if (rootIndex >= roots_.size())
            return;
        auto &root = roots_[rootIndex];
        const int descriptor = add_watch(root.parent, true);
        if (descriptor < 0)
        {
            root.parentDescriptor = -1;
            notify(ProjectChangeKind::NotificationsUnavailable, root.path, true);
            return;
        }
        root.parentDescriptor = descriptor;
        auto &rootIndexes = parentRootsByWatch_[descriptor];
        if (std::find(rootIndexes.begin(), rootIndexes.end(), rootIndex) == rootIndexes.end())
            rootIndexes.push_back(rootIndex);
    }

    void register_recovery_watch(std::size_t rootIndex)
    {
        if (rootIndex >= roots_.size() || roots_[rootIndex].recoveryParent.empty())
            return;
        auto &root = roots_[rootIndex];
        const int descriptor = add_watch(root.recoveryParent, false);
        if (descriptor < 0)
        {
            root.recoveryDescriptor = -1;
            notify(ProjectChangeKind::NotificationsUnavailable, root.path, true);
            return;
        }
        root.recoveryDescriptor = descriptor;
        auto &rootIndexes = recoveryRootsByWatch_[descriptor];
        if (std::find(rootIndexes.begin(), rootIndexes.end(), rootIndex) == rootIndexes.end())
            rootIndexes.push_back(rootIndex);
    }

    void register_nested_root_watches(const std::filesystem::path &recoveredTree)
    {
        for (std::size_t index = 0; index < roots_.size(); ++index)
        {
            auto &root = roots_[index];
            if (root.parentDescriptor < 0 && contains_path(recoveredTree, root.parent))
                register_parent_watch(index);
            if (root.recoveryDescriptor < 0 && !root.recoveryParent.empty() &&
                contains_path(recoveredTree, root.recoveryParent))
                register_recovery_watch(index);
        }
    }

    void recover_root_parent(std::size_t rootIndex)
    {
        if (rootIndex >= roots_.size())
            return;
        auto &root = roots_[rootIndex];
        std::error_code error;
        const auto parentStatus = std::filesystem::symlink_status(root.parent, error);
        if (error || !std::filesystem::is_directory(parentStatus) ||
            std::filesystem::is_symlink(parentStatus))
        {
            notify(ProjectChangeKind::NotificationsUnavailable, root.path, true);
            return;
        }

        register_parent_watch(rootIndex);
        if (root.parentDescriptor < 0)
            return;

        error.clear();
        const auto rootStatus = std::filesystem::symlink_status(root.path, error);
        if (!error && std::filesystem::is_directory(rootStatus) &&
            !std::filesystem::is_symlink(rootStatus))
        {
            if (add_tree(root.path))
            {
                register_nested_root_watches(root.path);
                notify(ProjectChangeKind::RootAvailable, root.path, true);
                notify(ProjectChangeKind::Overflow, root.path, true);
            }
            else
                notify(ProjectChangeKind::NotificationsUnavailable, root.path, true);
            return;
        }

        if (error == std::errc::no_such_file_or_directory)
            notify(ProjectChangeKind::RootUnavailable, root.path, true);
        else
            notify(ProjectChangeKind::NotificationsUnavailable, root.path, true);
    }

    void schedule_root_recovery(std::size_t rootIndex)
    {
        if (std::find(pendingRootRecoveries_.begin(), pendingRootRecoveries_.end(), rootIndex) ==
            pendingRootRecoveries_.end())
            pendingRootRecoveries_.push_back(rootIndex);
    }

    void finish_pending_root_recoveries()
    {
        auto pending = std::move(pendingRootRecoveries_);
        pendingRootRecoveries_.clear();
        for (const auto rootIndex : pending)
            if (!stopping_.load(std::memory_order_acquire))
                recover_root_parent(rootIndex);
    }

    static bool contains_path(const std::filesystem::path &parent,
                              const std::filesystem::path &candidate)
    {
        const auto relative = candidate.lexically_relative(parent);
        if (relative.empty())
            return candidate == parent;
        for (const auto &part : relative)
            if (part == "..")
                return false;
        return true;
    }

    bool belongs_to_watched_tree(const std::filesystem::path &directory) const
    {
        return std::any_of(roots_.begin(), roots_.end(), [&](const RootWatch &root) {
            return contains_path(root.path, directory);
        });
    }

    void remove_tree_watches(const std::filesystem::path &path)
    {
        std::vector<int> remove;
        for (const auto &[descriptor, watchedPath] : pathsByWatch_)
            if (contains_path(path, watchedPath))
                remove.push_back(descriptor);
        for (const int descriptor : remove)
            remove_watch(descriptor);
    }

    void remap_tree_watches(const std::filesystem::path &oldPath,
                            const std::filesystem::path &newPath)
    {
        std::vector<std::pair<int, std::filesystem::path>> remap;
        for (const auto &[descriptor, watchedPath] : pathsByWatch_)
        {
            if (!contains_path(oldPath, watchedPath))
                continue;
            const auto relative = watchedPath.lexically_relative(oldPath);
            remap.emplace_back(descriptor, relative.empty() ? newPath : newPath / relative);
        }
        for (const auto &[descriptor, path] : remap)
            watchesByPath_.erase(pathsByWatch_[descriptor].generic_string());
        for (const auto &[descriptor, path] : remap)
        {
            pathsByWatch_[descriptor] = path.lexically_normal();
            watchesByPath_[pathsByWatch_[descriptor].generic_string()] = descriptor;
        }
    }

    void notify(ProjectChangeKind kind, const std::filesystem::path &path,
                bool directory = false, const std::filesystem::path &oldPath = {})
    {
        if (!callback_ || stopping_.load(std::memory_order_acquire))
            return;
        ProjectChangeEvent event;
        event.kind = kind;
        event.path = path;
        event.oldPath = oldPath;
        event.directory = directory;
        event.projectGeneration = generation_;
        try
        {
            callback_(std::move(event));
        }
        catch (...)
        {
            // A failed publication is represented as loss and requires reconciliation.
            if (kind != ProjectChangeKind::Overflow)
            {
                ProjectChangeEvent overflow;
                overflow.kind = ProjectChangeKind::Overflow;
                overflow.projectGeneration = generation_;
                try { callback_(std::move(overflow)); } catch (...) {}
            }
        }
    }

    void expire_moves()
    {
        const auto now = std::chrono::steady_clock::now();
        for (auto move = pendingMoves_.begin(); move != pendingMoves_.end();)
        {
            if (now - move->second.time < std::chrono::milliseconds(150))
            {
                ++move;
                continue;
            }
            notify(ProjectChangeKind::Removed, move->second.path, move->second.directory);
            if (move->second.directory)
                remove_tree_watches(move->second.path);
            move = pendingMoves_.erase(move);
        }
    }

    void process_event(const inotify_event &event)
    {
        if (event.mask & IN_Q_OVERFLOW)
        {
            notify(ProjectChangeKind::Overflow, {}, false);
            return;
        }
        if (event.mask & IN_IGNORED)
        {
            const auto watched = pathsByWatch_.find(event.wd);
            const std::filesystem::path directory = watched == pathsByWatch_.end()
                ? std::filesystem::path{} : watched->second;
            const bool wasRoot = !directory.empty() && std::any_of(roots_.begin(), roots_.end(),
                [&](const RootWatch &root) { return directory == root.path; });
            remove_watch(event.wd);
            if (wasRoot)
                notify(ProjectChangeKind::RootUnavailable, directory, true);
            return;
        }

        bool rootTransitionHandled = false;
        const auto parentRoots = parentRootsByWatch_.find(event.wd);
        if (parentRoots != parentRootsByWatch_.end())
        {
            if (event.len > 0)
            {
                const std::filesystem::path entryName(event.name);
                const auto rootsOnParent = parentRoots->second;
                for (const std::size_t rootIndex : rootsOnParent)
                {
                    if (rootIndex >= roots_.size())
                        continue;
                    const auto &root = roots_[rootIndex];
                    if (root.name != entryName)
                        continue;
                    rootTransitionHandled = true;
                    if (event.mask & (IN_DELETE | IN_MOVED_FROM | IN_DELETE_SELF))
                    {
                        remove_tree_watches(root.path);
                        notify(ProjectChangeKind::RootUnavailable, root.path, true);
                    }
                    if (event.mask & (IN_CREATE | IN_MOVED_TO))
                    {
                        schedule_root_recovery(rootIndex);
                    }
                }
            }
            // A directory can be both a configured root's parent and another
            // configured root's watched tree (for example, a project root with
            // a separately routed nested assets root). Only consume events that
            // actually describe a configured root transition. Other entries in
            // that directory still need to flow through the ordinary tree watch.
        }
        const auto recoveryRoots = recoveryRootsByWatch_.find(event.wd);
        if (recoveryRoots != recoveryRootsByWatch_.end() && event.len > 0)
        {
            const std::filesystem::path entryName(event.name);
            const auto rootsOnRecovery = recoveryRoots->second;
            for (const std::size_t rootIndex : rootsOnRecovery)
            {
                if (rootIndex >= roots_.size() || roots_[rootIndex].recoveryName != entryName)
                    continue;
                rootTransitionHandled = true;
                const auto rootPath = roots_[rootIndex].path;
                if (event.mask & (IN_DELETE | IN_MOVED_FROM))
                {
                    remove_tree_watches(rootPath);
                    const int parentDescriptor = roots_[rootIndex].parentDescriptor;
                    if (parentDescriptor >= 0)
                        remove_watch(parentDescriptor);
                    notify(ProjectChangeKind::RootUnavailable, rootPath, true);
                }
                if (event.mask & (IN_CREATE | IN_MOVED_TO))
                    schedule_root_recovery(rootIndex);
            }
        }
        if (rootTransitionHandled)
            return;

        const auto watched = pathsByWatch_.find(event.wd);
        if (watched == pathsByWatch_.end())
            return;
        const auto directory = watched->second;
        if (event.mask & (IN_DELETE_SELF | IN_MOVE_SELF))
        {
            for (const auto &root : roots_)
                if (directory == root.path)
                {
                    remove_tree_watches(root.path);
                    notify(ProjectChangeKind::RootUnavailable, root.path, true);
                    break;
                }
            return;
        }
        if (event.len == 0)
            return;
        if (!belongs_to_watched_tree(directory))
            return;
        const std::filesystem::path path = (directory / event.name).lexically_normal();
        const bool isDirectory = (event.mask & IN_ISDIR) != 0;
        if (event.mask & IN_MOVED_FROM)
        {
            if (pendingMoves_.size() >= 4096)
            {
                pendingMoves_.clear();
                notify(ProjectChangeKind::Overflow, {}, false);
            }
            pendingMoves_[event.cookie] = PendingMove{path, isDirectory, std::chrono::steady_clock::now()};
            return;
        }
        if (event.mask & IN_MOVED_TO)
        {
            const auto move = pendingMoves_.find(event.cookie);
            if (move != pendingMoves_.end())
            {
                if (isDirectory)
                {
                    if (skipped_source_directory(path))
                        remove_tree_watches(move->second.path);
                    else
                        remap_tree_watches(move->second.path, path);
                }
                notify(ProjectChangeKind::Renamed, path, isDirectory, move->second.path);
                pendingMoves_.erase(move);
            }
            else
            {
                if (isDirectory && !skipped_source_directory(path) && !add_tree(path))
                    notify(ProjectChangeKind::NotificationsUnavailable, path, true);
                notify(ProjectChangeKind::Added, path, isDirectory);
            }
            return;
        }
        if (event.mask & IN_CREATE)
        {
            if (isDirectory && !skipped_source_directory(path) && !add_tree(path))
                notify(ProjectChangeKind::NotificationsUnavailable, path, true);
            notify(ProjectChangeKind::Added, path, isDirectory);
            return;
        }
        if (event.mask & IN_DELETE)
        {
            notify(ProjectChangeKind::Removed, path, isDirectory);
            if (isDirectory)
                remove_tree_watches(path);
            return;
        }
        if (event.mask & (IN_CLOSE_WRITE | IN_MODIFY | IN_ATTRIB))
            notify(ProjectChangeKind::Modified, path, isDirectory);
    }

    void process_event_buffer(const char *buffer, std::size_t size)
    {
        std::size_t offset = 0;
        while (offset + sizeof(inotify_event) <= size)
        {
            const auto *event = reinterpret_cast<const inotify_event *>(buffer + offset);
            if (offset + sizeof(inotify_event) + event->len > size)
            {
                notify(ProjectChangeKind::Overflow, {}, false);
                break;
            }
            process_event(*event);
            offset += sizeof(inotify_event) + event->len;
        }
    }

    bool drain_ignored_events_before_recovery(std::array<char, 64U * 1024U> &buffer)
    {
        bool queueDrained = false;
        for (std::size_t reads = 0; reads < 16 && !stopping_.load(std::memory_order_acquire); ++reads)
        {
            const ssize_t size = read(fd_, buffer.data(), buffer.size());
            if (size < 0)
            {
                if (errno == EINTR)
                    continue;
                if (errno == EAGAIN)
                    queueDrained = true;
                else
                    notify(ProjectChangeKind::NotificationsUnavailable, {}, false);
                break;
            }
            if (size == 0)
            {
                queueDrained = true;
                break;
            }
            process_event_buffer(buffer.data(), static_cast<std::size_t>(size));
        }
        return queueDrained;
    }

    void run()
    {
        fd_ = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
        if (fd_ < 0)
        {
            notify(ProjectChangeKind::NotificationsUnavailable, {}, false);
            return;
        }
        for (std::size_t index = 0; index < roots_.size(); ++index)
        {
            if (stopping_.load(std::memory_order_acquire))
                break;
            auto &root = roots_[index];
            register_recovery_watch(index);
            register_parent_watch(index);
            const bool treeReady = add_tree(root.path);
            std::error_code rootError;
            const auto rootStatus = std::filesystem::symlink_status(root.path, rootError);
            const bool rootAvailable = !rootError && std::filesystem::is_directory(rootStatus) &&
                                       !std::filesystem::is_symlink(rootStatus);
            if (stopping_.load(std::memory_order_acquire))
                break;
            if (treeReady && rootAvailable)
            {
                register_nested_root_watches(root.path);
                notify(ProjectChangeKind::RootAvailable, root.path, true);
                // Events can occur before individual watches are installed.
                // Reconcile after discovery so each inventory adopts only after
                // native coverage is in place.
                notify(ProjectChangeKind::Overflow, root.path, true);
            }
            else if (treeReady && !rootAvailable &&
                     rootError == std::errc::no_such_file_or_directory)
                notify(ProjectChangeKind::RootUnavailable, root.path, true);
            else
                notify(ProjectChangeKind::NotificationsUnavailable, root.path, true);
        }

        std::array<char, 64U * 1024U> buffer{};
        pollfd descriptor{fd_, POLLIN, 0};
        while (!stopping_.load(std::memory_order_acquire))
        {
            const int ready = poll(&descriptor, 1, 100);
            if (ready < 0)
            {
                if (errno == EINTR)
                    continue;
                notify(ProjectChangeKind::NotificationsUnavailable, {}, false);
                break;
            }
            if (ready == 0)
            {
                expire_moves();
                if (!pendingRootRecoveries_.empty())
                    finish_pending_root_recoveries();
                continue;
            }
            const ssize_t size = read(fd_, buffer.data(), buffer.size());
            if (size < 0)
            {
                if (errno != EAGAIN && errno != EINTR)
                    notify(ProjectChangeKind::NotificationsUnavailable, {}, false);
                else if (!pendingRootRecoveries_.empty() && errno == EAGAIN)
                    finish_pending_root_recoveries();
                continue;
            }
            process_event_buffer(buffer.data(), static_cast<std::size_t>(size));
            if (!pendingRootRecoveries_.empty() &&
                drain_ignored_events_before_recovery(buffer))
                finish_pending_root_recoveries();
            expire_moves();
        }
        if (fd_ >= 0)
        {
            close(fd_);
            fd_ = -1;
        }
        pathsByWatch_.clear();
        watchesByPath_.clear();
        pendingMoves_.clear();
        pendingRootRecoveries_.clear();
        parentRootsByWatch_.clear();
        recoveryRootsByWatch_.clear();
        if (budget_ && activeWatchCount_ > 0)
            budget_->release(activeWatchCount_);
        activeWatchCount_ = 0;
        reportedWatchCount_.store(0, std::memory_order_release);
    }

    int fd_ = -1;
    std::filesystem::path root_;
    std::vector<RootWatch> roots_;
    ProjectGeneration generation_;
    Callback callback_;
    std::atomic<bool> stopping_{false};
    std::atomic<std::size_t> reportedWatchCount_{0};
    std::thread worker_;
    std::unordered_map<int, std::filesystem::path> pathsByWatch_;
    std::unordered_map<std::string, int> watchesByPath_;
    std::unordered_map<int, std::vector<std::size_t>> parentRootsByWatch_;
    std::unordered_map<int, std::vector<std::size_t>> recoveryRootsByWatch_;
    std::unordered_map<std::uint32_t, PendingMove> pendingMoves_;
    std::vector<std::size_t> pendingRootRecoveries_;
    std::size_t activeWatchCount_ = 0;
    bool sourceInputsOnly_ = false;
    std::shared_ptr<ProjectWatchBudget> budget_;
    std::vector<std::filesystem::path> includedSubtrees_;
};
} // namespace

std::unique_ptr<ProjectWatchService> make_project_watch_service()
{
    return std::make_unique<LinuxProjectWatchService>();
}
} // namespace platformator_editor
