#include "watch.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace platformator_editor
{
namespace
{
class WindowsProjectWatchService final : public ProjectWatchService
{
  public:
    ~WindowsProjectWatchService() override { stop(); }

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
            error = "too many roots were requested for the Windows project watcher";
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
            root.name = rootPath.filename().native();
            roots_.push_back(std::move(root));
        }
        if (roots_.empty())
        {
            error = "no usable project watch roots were supplied";
            return false;
        }
        generation_ = generation;
        callback_ = std::move(callback);
        for (auto &root : roots_)
        {
            if (!open_watch(root.rootWatch, root.path, true, error) ||
                !open_watch(root.parentWatch, root.parent, false, error))
            {
                stop();
                return false;
            }
        }
        stopping_.store(false, std::memory_order_release);
        try
        {
            worker_ = std::thread([this] { run(); });
        }
        catch (const std::exception &exception)
        {
            error = std::string("cannot start ReadDirectoryChangesW worker: ") + exception.what();
            stop();
            return false;
        }
        return true;
    }

    void stop() noexcept override
    {
        stopping_.store(true, std::memory_order_release);
        {
            std::lock_guard lock(watchMutex_);
            for (auto &root : roots_)
            {
                cancel_watch(root.rootWatch);
                cancel_watch(root.parentWatch);
            }
        }
        if (worker_.joinable())
            worker_.join();
        {
            std::lock_guard lock(watchMutex_);
            for (auto &root : roots_)
            {
                wait_for_cancel(root.rootWatch);
                wait_for_cancel(root.parentWatch);
                close_watch(root.rootWatch);
                close_watch(root.parentWatch);
            }
        }
        callback_ = {};
        roots_.clear();
    }

    std::size_t watched_directories() const noexcept override
    {
        return activeWatchCount_.load(std::memory_order_acquire);
    }
    bool supported() const noexcept override { return true; }

  private:
    static constexpr std::size_t MaximumRoots = 8;

    struct Watch
    {
        HANDLE directory = INVALID_HANDLE_VALUE;
        HANDLE event = nullptr;
        OVERLAPPED overlapped{};
        std::array<std::byte, 64U * 1024U> buffer{};
        DWORD bytes = 0;
        bool recursive = false;
        bool parent = false;
        bool pending = false;
        std::shared_ptr<ProjectWatchBudget> budget;
        bool budgetReserved = false;
        bool activeCounted = false;
    };

    struct Root
    {
        std::filesystem::path path;
        std::filesystem::path parent;
        std::wstring name;
        Watch rootWatch;
        Watch parentWatch;
        std::filesystem::path pendingOldName;
        bool pendingOldDirectory = false;
        std::chrono::steady_clock::time_point pendingOldTime{};
    };

    void close_watch(Watch &watch) noexcept
    {
        if (watch.event != nullptr)
            CloseHandle(watch.event);
        if (watch.directory != INVALID_HANDLE_VALUE)
            CloseHandle(watch.directory);
        if (watch.budgetReserved && watch.budget)
            watch.budget->release();
        if (watch.activeCounted)
            activeWatchCount_.fetch_sub(1, std::memory_order_acq_rel);
        watch = {};
    }

    static void cancel_watch(Watch &watch) noexcept
    {
        if (watch.directory != INVALID_HANDLE_VALUE)
            CancelIoEx(watch.directory, &watch.overlapped);
    }

    static void wait_for_cancel(Watch &watch) noexcept
    {
        if (watch.pending && watch.directory != INVALID_HANDLE_VALUE)
        {
            DWORD completedBytes = 0;
            GetOverlappedResult(watch.directory, &watch.overlapped, &completedBytes, TRUE);
            watch.pending = false;
        }
    }

    static void cancel_and_drain(Watch &watch) noexcept
    {
        if (watch.directory == INVALID_HANDLE_VALUE || !watch.pending)
            return;
        CancelIoEx(watch.directory, &watch.overlapped);
        wait_for_cancel(watch);
    }

    bool open_watch(Watch &watch, const std::filesystem::path &path, bool recursive,
                    std::string &error)
    {
        if (budget_ && !budget_->try_acquire())
        {
            error = "the shared project watch budget is exhausted";
            return false;
        }
        watch.budget = budget_;
        watch.budgetReserved = static_cast<bool>(budget_);
        watch.directory = CreateFileW(path.c_str(), FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        if (watch.directory == INVALID_HANDLE_VALUE)
        {
            error = "CreateFileW could not open a directory for notifications (" +
                    std::to_string(GetLastError()) + ")";
            return false;
        }
        watch.event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (watch.event == nullptr)
        {
            error = "CreateEventW failed for directory notifications";
            return false;
        }
        watch.recursive = recursive;
        watch.parent = !recursive;
        return issue_read(watch, error);
    }

    bool issue_read(Watch &watch, std::string &error)
    {
        ResetEvent(watch.event);
        watch.overlapped = {};
        watch.overlapped.hEvent = watch.event;
        constexpr DWORD filter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                 FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE |
                                 FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_CREATION;
        if (!ReadDirectoryChangesW(watch.directory, watch.buffer.data(),
                static_cast<DWORD>(watch.buffer.size()), watch.recursive, filter, nullptr,
                &watch.overlapped, nullptr) && GetLastError() != ERROR_IO_PENDING)
        {
            error = "ReadDirectoryChangesW could not arm a notification request (" +
                    std::to_string(GetLastError()) + ")";
            return false;
        }
        watch.pending = true;
        if (!watch.activeCounted)
        {
            watch.activeCounted = true;
            activeWatchCount_.fetch_add(1, std::memory_order_acq_rel);
        }
        return true;
    }

    void notify(ProjectChangeKind kind, const std::filesystem::path &path,
                bool directory = false, const std::filesystem::path &oldPath = {})
    {
        if (!callback_)
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
            ProjectChangeEvent overflow;
            overflow.kind = ProjectChangeKind::Overflow;
            overflow.projectGeneration = generation_;
            try { callback_(std::move(overflow)); } catch (...) {}
        }
    }

    void process(Root &root, Watch &watch, DWORD bytes)
    {
        if (bytes == 0)
        {
            notify(ProjectChangeKind::Overflow, root.path, true);
            return;
        }
        if (watch.parent)
        {
            auto *record = reinterpret_cast<FILE_NOTIFY_INFORMATION *>(watch.buffer.data());
            while (record != nullptr)
            {
                const std::wstring_view name(record->FileName, record->FileNameLength / sizeof(wchar_t));
                if (name == root.name)
                {
                    if (record->Action == FILE_ACTION_REMOVED ||
                        record->Action == FILE_ACTION_RENAMED_OLD_NAME)
                    {
                        retire_root_watch(root);
                        notify(ProjectChangeKind::RootUnavailable, root.path, true);
                    }
                    else if (record->Action == FILE_ACTION_ADDED ||
                             record->Action == FILE_ACTION_RENAMED_NEW_NAME)
                        rearm_root_watch(root);
                }
                if (record->NextEntryOffset == 0)
                    break;
                record = reinterpret_cast<FILE_NOTIFY_INFORMATION *>(
                    reinterpret_cast<std::byte *>(record) + record->NextEntryOffset);
            }
            return;
        }

        auto *record = reinterpret_cast<FILE_NOTIFY_INFORMATION *>(watch.buffer.data());
        while (record != nullptr)
        {
            const std::wstring_view name(record->FileName, record->FileNameLength / sizeof(wchar_t));
            const std::filesystem::path path = (root.path / std::wstring(name)).lexically_normal();
            const DWORD attributes = GetFileAttributesW(path.c_str());
            const bool directory = attributes != INVALID_FILE_ATTRIBUTES &&
                                   (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            const bool reparsePoint = attributes != INVALID_FILE_ATTRIBUTES &&
                                      (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
            switch (record->Action)
            {
            case FILE_ACTION_ADDED:
                notify(ProjectChangeKind::Added, path, directory);
                break;
            case FILE_ACTION_REMOVED:
                // The entry is gone by the time its attributes can be queried. Treating
                // removals as possible directories safely invalidates a removed subtree.
                notify(ProjectChangeKind::Removed, path, true);
                break;
            case FILE_ACTION_MODIFIED:
                notify(ProjectChangeKind::Modified, path, directory);
                break;
            case FILE_ACTION_RENAMED_OLD_NAME:
                if (!root.pendingOldName.empty())
                {
                    root.pendingOldName.clear();
                    notify(ProjectChangeKind::Overflow, root.path, true);
                }
                root.pendingOldName = path;
                root.pendingOldDirectory = directory;
                root.pendingOldTime = std::chrono::steady_clock::now();
                break;
            case FILE_ACTION_RENAMED_NEW_NAME:
                if (!root.pendingOldName.empty())
                {
                    notify(ProjectChangeKind::Renamed, path, directory || root.pendingOldDirectory,
                           root.pendingOldName);
                    root.pendingOldName.clear();
                }
                else
                    notify(ProjectChangeKind::Added, path, directory);
                break;
            default:
                notify(ProjectChangeKind::Overflow, root.path, true);
                break;
            }
            if (directory && !reparsePoint &&
                (record->Action == FILE_ACTION_ADDED || record->Action == FILE_ACTION_RENAMED_NEW_NAME))
            {
                // Recursive requests cover the new subtree; uncertainty forces a bounded listing replay
                // when the filesystem did not report every moved-in descendant.
                notify(ProjectChangeKind::Overflow, path, true);
            }
            if (record->NextEntryOffset == 0)
                break;
            record = reinterpret_cast<FILE_NOTIFY_INFORMATION *>(
                reinterpret_cast<std::byte *>(record) + record->NextEntryOffset);
        }
    }

    void rearm_root_watch(Root &root)
    {
        std::lock_guard lock(watchMutex_);
        if (stopping_.load(std::memory_order_acquire))
            return;
        if (root.rootWatch.directory != INVALID_HANDLE_VALUE)
        {
            cancel_and_drain(root.rootWatch);
            close_watch(root.rootWatch);
        }
        std::error_code errorCode;
        if (!std::filesystem::is_directory(root.path, errorCode) || errorCode)
        {
            notify(ProjectChangeKind::RootUnavailable, root.path, true);
            return;
        }
        std::string error;
        if (!open_watch(root.rootWatch, root.path, true, error))
        {
            close_watch(root.rootWatch);
            notify(ProjectChangeKind::NotificationsUnavailable, root.path, true);
            notify(ProjectChangeKind::Overflow, root.path, true);
            return;
        }
        notify(ProjectChangeKind::RootAvailable, root.path, true);
        notify(ProjectChangeKind::Overflow, root.path, true);
    }

    void retire_root_watch(Root &root)
    {
        std::lock_guard lock(watchMutex_);
        if (stopping_.load(std::memory_order_acquire))
            return;
        cancel_and_drain(root.rootWatch);
        close_watch(root.rootWatch);
    }

    void run()
    {
        while (!stopping_.load(std::memory_order_acquire))
        {
            struct PendingWatch
            {
                Root *root;
                Watch *watch;
            };
            std::array<PendingWatch, MaximumRoots * 2> pendingWatches{};
            std::array<HANDLE, MaximumRoots * 2> events{};
            DWORD eventCount = 0;
            for (auto &root : roots_)
            {
                Watch *rootWatches[]{&root.rootWatch, &root.parentWatch};
                for (Watch *watch : rootWatches)
                {
                    if (watch->event == nullptr || !watch->pending)
                        continue;
                    pendingWatches[eventCount] = {&root, watch};
                    events[eventCount] = watch->event;
                    ++eventCount;
                }
            }
            if (eventCount == 0)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            const DWORD ready = WaitForMultipleObjects(eventCount, events.data(), FALSE, 100);
            if (ready == WAIT_TIMEOUT)
            {
                for (auto &root : roots_)
                {
                    if (!root.pendingOldName.empty() &&
                        std::chrono::steady_clock::now() - root.pendingOldTime >
                            std::chrono::milliseconds(150))
                    {
                        notify(ProjectChangeKind::Removed, root.pendingOldName,
                               root.pendingOldDirectory);
                        root.pendingOldName.clear();
                    }
                }
                continue;
            }
            if (ready >= WAIT_OBJECT_0 + eventCount)
            {
                notify(ProjectChangeKind::NotificationsUnavailable, {}, false);
                break;
            }
            const auto selected = static_cast<std::size_t>(ready - WAIT_OBJECT_0);
            Root &root = *pendingWatches[selected].root;
            Watch &watch = *pendingWatches[selected].watch;
            DWORD bytes = 0;
            if (!GetOverlappedResult(watch.directory, &watch.overlapped, &bytes, FALSE))
            {
                const DWORD error = GetLastError();
                watch.pending = false;
                if (error == ERROR_NOTIFY_ENUM_DIR && !stopping_.load(std::memory_order_acquire))
                    notify(ProjectChangeKind::Overflow, root.path, true);
                else if (error != ERROR_OPERATION_ABORTED &&
                         !stopping_.load(std::memory_order_acquire))
                {
                    // This worker owns every configured root; losing one read loop
                    // stops the shared owner, so report global coverage loss.
                    notify(ProjectChangeKind::NotificationsUnavailable, {}, false);
                    stopping_.store(true, std::memory_order_release);
                    break;
                }
            }
            else
            {
                watch.pending = false;
                process(root, watch, bytes);
            }
            if (!stopping_.load(std::memory_order_acquire) &&
                watch.directory != INVALID_HANDLE_VALUE && watch.event != nullptr)
            {
                std::string error;
                bool armed = false;
                {
                    std::lock_guard lock(watchMutex_);
                    if (!stopping_.load(std::memory_order_acquire))
                        armed = issue_read(watch, error);
                }
                if (!armed && !stopping_.load(std::memory_order_acquire))
                {
                    notify(ProjectChangeKind::NotificationsUnavailable, {}, false);
                    stopping_.store(true, std::memory_order_release);
                    break;
                }
            }
        }
    }

    std::vector<Root> roots_;
    ProjectGeneration generation_;
    Callback callback_;
    std::atomic<bool> stopping_{false};
    std::atomic<std::size_t> activeWatchCount_{0};
    std::mutex watchMutex_;
    std::thread worker_;
    std::shared_ptr<ProjectWatchBudget> budget_;
};
} // namespace

std::unique_ptr<ProjectWatchService> make_project_watch_service()
{
    return std::make_unique<WindowsProjectWatchService>();
}
} // namespace platformator_editor
