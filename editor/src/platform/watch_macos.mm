#include "watch.h"

#include <CoreServices/CoreServices.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace platformator_editor
{
namespace
{
class MacProjectWatchService final : public ProjectWatchService
{
  public:
    ~MacProjectWatchService() override { stop(); }

    bool start(ProjectGeneration generation, const std::filesystem::path &projectRoot,
               Callback callback, std::string &error, ProjectWatchOptions options) override
    {
        stop();
        budget_ = std::move(options.budget);
        std::vector<std::filesystem::path> requestedRoots{projectRoot};
        requestedRoots.insert(requestedRoots.end(), options.additionalRoots.begin(),
                              options.additionalRoots.end());
        if (requestedRoots.size() > MaximumRoots)
        {
            error = "too many roots were requested for the macOS project watcher";
            return false;
        }
        for (const auto &requestedRoot : requestedRoots)
        {
            std::error_code filesystemError;
            const auto rootPath = std::filesystem::canonical(requestedRoot, filesystemError);
            if (filesystemError || !std::filesystem::is_directory(rootPath, filesystemError) || filesystemError)
            {
                error = filesystemError ? filesystemError.message() : "watch root is not a directory";
                roots_.clear();
                return false;
            }
            if (std::any_of(roots_.begin(), roots_.end(), [&](const Root &root) {
                    return root.path == rootPath;
                }))
                continue;
            Root root;
            root.path = rootPath;
            root.parent = rootPath.parent_path();
            root.name = rootPath.filename();
            roots_.push_back(std::move(root));
        }
        if (roots_.empty())
        {
            error = "no usable project watch roots were supplied";
            return false;
        }
        generation_ = generation;
        callback_ = std::move(callback);
        {
            std::lock_guard lock(mutex_);
            stopping_ = false;
            ready_ = false;
            startError_.clear();
            runLoop_ = nullptr;
        }
        try
        {
            worker_ = std::thread([this] { run(); });
        }
        catch (const std::exception &exception)
        {
            error = std::string("cannot start FSEvents worker: ") + exception.what();
            callback_ = {};
            return false;
        }
        std::unique_lock lock(mutex_);
        condition_.wait(lock, [&] { return ready_; });
        if (!startError_.empty())
        {
            error = startError_;
            lock.unlock();
            if (worker_.joinable())
                worker_.join();
            callback_ = {};
            return false;
        }
        return true;
    }

    void stop() noexcept override
    {
        CFRunLoopRef loop = nullptr;
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
            loop = runLoop_;
            if (loop != nullptr)
                CFRetain(loop);
        }
        if (loop != nullptr)
        {
            CFRunLoopStop(loop);
            CFRunLoopWakeUp(loop);
            CFRelease(loop);
        }
        if (worker_.joinable())
            worker_.join();
        {
            std::lock_guard lock(mutex_);
            runLoop_ = nullptr;
        }
        callback_ = {};
        roots_.clear();
    }

    std::size_t watched_directories() const noexcept override
    {
        return watchedPathCount_.load(std::memory_order_acquire);
    }
    bool supported() const noexcept override { return true; }

  private:
    static constexpr std::size_t MaxEventsPerCallback = 256;
    static constexpr std::size_t MaximumRoots = 8;

    struct Root
    {
        std::filesystem::path path;
        std::filesystem::path parent;
        std::filesystem::path name;
        bool available = false;
    };

    static void stream_callback(ConstFSEventStreamRef, void *context, std::size_t count,
                                void *eventPaths, const FSEventStreamEventFlags flags[],
                                const FSEventStreamEventId[]) noexcept
    {
        auto *self = static_cast<MacProjectWatchService *>(context);
        try
        {
            if (count == 0)
                return;
            if (eventPaths == nullptr || flags == nullptr)
            {
                for (const auto &root : self->roots_)
                    self->notify(ProjectChangeKind::Overflow, root.path, true);
                return;
            }
            auto **paths = static_cast<char **>(eventPaths);
            const std::size_t boundedCount = std::min(count, MaxEventsPerCallback);
            for (std::size_t index = 0; index < boundedCount; ++index)
                self->handle(paths[index] == nullptr ? std::filesystem::path{} :
                             std::filesystem::path(paths[index]), flags[index]);
            if (boundedCount != count)
                for (const auto &root : self->roots_)
                    self->notify(ProjectChangeKind::Overflow, root.path, true);
        }
        catch (...)
        {
            // Never allow a C++ exception to escape through the Core Foundation
            // callback boundary; a lost path hint is repaired by root reconciliation.
            self->notify(ProjectChangeKind::Overflow, {}, true);
        }
    }

    void notify(ProjectChangeKind kind, const std::filesystem::path &path, bool directory = false)
    {
        if (!callback_)
            return;
        ProjectChangeEvent event;
        event.kind = kind;
        event.path = path;
        event.directory = directory;
        event.projectGeneration = generation_;
        try
        {
            callback_(std::move(event));
        }
        catch (...)
        {
            ProjectChangeEvent overflow;
            overflow.kind = ProjectChangeKind::Overflow;
            overflow.projectGeneration = generation_;
            try { callback_(std::move(overflow)); } catch (...) {}
        }
    }

    void handle(const std::filesystem::path &path, FSEventStreamEventFlags flags)
    {
        constexpr FSEventStreamEventFlags lostEvents = kFSEventStreamEventFlagMustScanSubDirs |
            kFSEventStreamEventFlagUserDropped | kFSEventStreamEventFlagKernelDropped;
        const auto contains = [](const std::filesystem::path &parent,
                                 const std::filesystem::path &candidate) {
            const auto relative = candidate.lexically_normal().lexically_relative(parent.lexically_normal());
            if (relative.empty())
                return candidate.lexically_normal() == parent.lexically_normal();
            return !relative.is_absolute() && *relative.begin() != "..";
        };
        Root *matchedRoot = nullptr;
        bool rootEntry = false;
        for (auto &root : roots_)
        {
            const bool isRoot = path == root.path;
            const bool isRootEntry = path.parent_path() == root.parent && path.filename() == root.name;
            if (isRoot || isRootEntry || contains(root.path, path))
            {
                if (matchedRoot == nullptr || root.path.native().size() > matchedRoot->path.native().size())
                {
                    matchedRoot = &root;
                    rootEntry = isRootEntry;
                }
            }
        }
        if (path.empty())
        {
            for (const auto &root : roots_)
                notify(ProjectChangeKind::Overflow, root.path, true);
            if ((flags & kFSEventStreamEventFlagRootChanged) != 0)
                rearmRequested_.store(true, std::memory_order_release);
            return;
        }
        if (matchedRoot == nullptr)
        {
            // FSEvents also reports unrelated siblings from watched parent paths.
            // Only stream-loss/root-change metadata requires a conservative replay
            // when no configured tree owns the reported path.
            if ((flags & (lostEvents | kFSEventStreamEventFlagRootChanged)) != 0)
            {
                for (const auto &root : roots_)
                    notify(ProjectChangeKind::Overflow, root.path, true);
                if ((flags & kFSEventStreamEventFlagRootChanged) != 0)
                    rearmRequested_.store(true, std::memory_order_release);
            }
            return;
        }

        Root &root = *matchedRoot;
        if ((flags & lostEvents) != 0)
        {
            notify(ProjectChangeKind::Overflow, path, true);
            if ((flags & kFSEventStreamEventFlagRootChanged) == 0)
                return;
        }
        if ((flags & kFSEventStreamEventFlagRootChanged) != 0)
        {
            std::error_code error;
            const auto status = std::filesystem::symlink_status(root.path, error);
            const bool available = !error && !std::filesystem::is_symlink(status) &&
                                   std::filesystem::is_directory(status);
            if (available)
            {
                // FSEvents can report a root change after a rename/rebind even when the
                // configured path still resolves. Reconcile that case without claiming
                // the project root is unavailable.
                root.available = true;
                notify(ProjectChangeKind::Overflow, root.path, true);
            }
            else if (error == std::errc::no_such_file_or_directory || !error)
            {
                root.available = false;
                notify(ProjectChangeKind::RootUnavailable, root.path, true);
            }
            else
            {
                // A transient status failure is uncertain coverage, not proof that the
                // project root was removed.
                notify(ProjectChangeKind::NotificationsUnavailable, root.path, true);
                notify(ProjectChangeKind::Overflow, root.path, true);
            }
            rearmRequested_.store(true, std::memory_order_release);
            return;
        }
        const bool directory = (flags & kFSEventStreamEventFlagItemIsDir) != 0;
        if (path == root.path || rootEntry)
        {
            if ((flags & kFSEventStreamEventFlagItemRemoved) != 0)
            {
                root.available = false;
                notify(ProjectChangeKind::RootUnavailable, root.path, true);
                rearmRequested_.store(true, std::memory_order_release);
            }
            else if ((flags & (kFSEventStreamEventFlagItemCreated |
                               kFSEventStreamEventFlagItemRenamed)) != 0)
                rearmRequested_.store(true, std::memory_order_release);
            else if ((flags & lostEvents) == 0)
                notify(ProjectChangeKind::Modified, root.path, true);
            return;
        }
        if ((flags & kFSEventStreamEventFlagItemRenamed) != 0)
        {
            // FSEvents does not guarantee both rename paths. Reconcile before publishing
            // any incomplete rename pair as an ordinary add/remove.
            notify(ProjectChangeKind::Overflow, path, directory);
            return;
        }
        if ((flags & kFSEventStreamEventFlagItemRemoved) != 0)
            notify(ProjectChangeKind::Removed, path, directory);
        else if ((flags & kFSEventStreamEventFlagItemCreated) != 0)
            notify(ProjectChangeKind::Added, path, directory);
        else if ((flags & (kFSEventStreamEventFlagItemModified |
                           kFSEventStreamEventFlagItemInodeMetaMod |
                           kFSEventStreamEventFlagItemChangeOwner |
                           kFSEventStreamEventFlagItemXattrMod |
                           kFSEventStreamEventFlagItemFinderInfoMod)) != 0)
            notify(ProjectChangeKind::Modified, path, directory);
        else
            notify(ProjectChangeKind::Overflow, root.path, true);
    }

    bool start_stream(CFRunLoopRef loop)
    {
        std::vector<std::filesystem::path> watchedPaths;
        std::vector<CFStringRef> pathStrings;
        CFArrayRef paths = nullptr;
        std::size_t reservedWatchCount = 0;
        try
        {
            for (const auto &root : roots_)
            {
                if (root.available &&
                    std::find(watchedPaths.begin(), watchedPaths.end(), root.path) == watchedPaths.end())
                    watchedPaths.push_back(root.path);
                if (std::find(watchedPaths.begin(), watchedPaths.end(), root.parent) == watchedPaths.end())
                    watchedPaths.push_back(root.parent);
            }
            if (watchedPaths.empty())
                return false;

            std::vector<const void *> values;
            pathStrings.reserve(watchedPaths.size());
            values.reserve(watchedPaths.size());
            for (const auto &path : watchedPaths)
            {
                const auto text = path.string();
                CFStringRef value = CFStringCreateWithBytes(kCFAllocatorDefault,
                    reinterpret_cast<const UInt8 *>(text.data()), static_cast<CFIndex>(text.size()),
                    kCFStringEncodingUTF8, false);
                if (value == nullptr)
                    break;
                pathStrings.push_back(value);
                values.push_back(value);
            }
            if (pathStrings.size() != watchedPaths.size())
                throw std::runtime_error("FSEvents could not encode a configured watch path");
            paths = CFArrayCreate(kCFAllocatorDefault, values.data(),
                                  static_cast<CFIndex>(values.size()), &kCFTypeArrayCallBacks);
            if (paths == nullptr)
                throw std::runtime_error("FSEvents could not allocate its watch path list");

            if (budget_)
            {
                while (reservedWatchCount < watchedPaths.size())
                {
                    if (!budget_->try_acquire())
                        throw std::runtime_error("the shared project watch budget is exhausted");
                    ++reservedWatchCount;
                }
            }

            FSEventStreamContext context{};
            context.info = this;
            constexpr FSEventStreamCreateFlags createFlags = kFSEventStreamCreateFlagFileEvents |
                kFSEventStreamCreateFlagWatchRoot | kFSEventStreamCreateFlagNoDefer;
            stream_ = FSEventStreamCreate(kCFAllocatorDefault, &stream_callback, &context, paths,
                kFSEventStreamEventIdSinceNow, 0.2, createFlags);
            if (stream_ != nullptr)
            {
                FSEventStreamScheduleWithRunLoop(stream_, loop, kCFRunLoopDefaultMode);
                if (!FSEventStreamStart(stream_))
                {
                    FSEventStreamInvalidate(stream_);
                    FSEventStreamRelease(stream_);
                    stream_ = nullptr;
                }
            }
        }
        catch (...)
        {
            if (stream_ != nullptr)
            {
                FSEventStreamStop(stream_);
                FSEventStreamInvalidate(stream_);
                FSEventStreamRelease(stream_);
                stream_ = nullptr;
            }
        }
        if (paths != nullptr)
            CFRelease(paths);
        for (CFStringRef value : pathStrings)
            CFRelease(value);
        const bool started = stream_ != nullptr;
        if (!started && reservedWatchCount > 0)
            budget_->release(reservedWatchCount);
        streamActive_.store(started, std::memory_order_release);
        watchedPathCount_.store(started ? watchedPaths.size() : 0U,
                                std::memory_order_release);
        return started;
    }

    void stop_stream()
    {
        if (stream_ != nullptr)
        {
            FSEventStreamStop(stream_);
            FSEventStreamInvalidate(stream_);
            FSEventStreamRelease(stream_);
            stream_ = nullptr;
        }
        const auto watchedPathCount = watchedPathCount_.exchange(0, std::memory_order_acq_rel);
        if (budget_ && watchedPathCount > 0)
            budget_->release(watchedPathCount);
        streamActive_.store(false, std::memory_order_release);
    }

    void run()
    {
        CFRunLoopRef loop = CFRunLoopGetCurrent();
        CFRetain(loop);
        {
            std::lock_guard lock(mutex_);
            runLoop_ = loop;
        }

        for (auto &root : roots_)
        {
            std::error_code rootError;
            const auto rootStatus = std::filesystem::symlink_status(root.path, rootError);
            root.available = !rootError && !std::filesystem::is_symlink(rootStatus) &&
                             std::filesystem::is_directory(rootStatus);
        }
        const bool streamStarted = start_stream(loop);

        {
            std::lock_guard lock(mutex_);
            if (!streamStarted)
                startError_ = "FSEvents could not monitor the configured roots and their parents";
            ready_ = true;
            condition_.notify_all();
        }
        bool shouldRun = false;
        {
            std::lock_guard lock(mutex_);
            shouldRun = streamStarted && !stopping_;
        }
        auto nextRearmAttempt = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (shouldRun && !is_stopping())
        {
            if (stream_ != nullptr)
                CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.2, true);
            else
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (rearmRequested_.exchange(false, std::memory_order_acq_rel) ||
                (stream_ == nullptr && std::chrono::steady_clock::now() >= nextRearmAttempt))
            {
                std::array<bool, MaximumRoots> wasAvailable{};
                for (std::size_t index = 0; index < roots_.size(); ++index)
                    wasAvailable[index] = roots_[index].available;
                stop_stream();
                for (auto &root : roots_)
                {
                    std::error_code error;
                    const auto rootStatus = std::filesystem::symlink_status(root.path, error);
                    root.available = !error && !std::filesystem::is_symlink(rootStatus) &&
                                     std::filesystem::is_directory(rootStatus);
                }
                if (!start_stream(loop))
                {
                    for (const auto &root : roots_)
                        notify(ProjectChangeKind::NotificationsUnavailable, root.path, true);
                    nextRearmAttempt = std::chrono::steady_clock::now() + std::chrono::seconds(1);
                    continue;
                }
                for (std::size_t index = 0; index < roots_.size(); ++index)
                {
                    auto &root = roots_[index];
                    if (root.available && !wasAvailable[index])
                        notify(ProjectChangeKind::RootAvailable, root.path, true);
                    if (root.available)
                        // Rebinding a stream can lose changes, so reconcile every
                        // still-available tree after rearming the shared owner.
                        notify(ProjectChangeKind::Overflow, root.path, true);
                }
                nextRearmAttempt = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            }
        }
        stop_stream();
        {
            std::lock_guard lock(mutex_);
            runLoop_ = nullptr;
        }
        CFRelease(loop);
    }

    bool is_stopping() const
    {
        std::lock_guard lock(mutex_);
        return stopping_;
    }

    std::vector<Root> roots_;
    ProjectGeneration generation_;
    Callback callback_;
    FSEventStreamRef stream_ = nullptr;
    std::thread worker_;
    std::atomic<bool> streamActive_{false};
    std::atomic<std::size_t> watchedPathCount_{0};
    std::atomic<bool> rearmRequested_{false};
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    CFRunLoopRef runLoop_ = nullptr;
    bool stopping_ = false;
    bool ready_ = false;
    std::string startError_;
    std::shared_ptr<ProjectWatchBudget> budget_;
};
} // namespace

std::unique_ptr<ProjectWatchService> make_project_watch_service()
{
    return std::make_unique<MacProjectWatchService>();
}
} // namespace platformator_editor
