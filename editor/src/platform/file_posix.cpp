#include "nativefile.h"
#include "nativefile_testhooks.h"

#include <atomic>
#include <cerrno>
#include <cstring>
#include <limits>
#include <mutex>
#include <system_error>
#include <utility>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace platformator_editor
{
namespace
{
std::atomic<std::uint64_t> temporarySequence{0};
std::mutex testHooksMutex;
NativeFileTestHooks testHooks;

NativeFileTestHooks current_test_hooks()
{
    std::lock_guard lock(testHooksMutex);
    return testHooks;
}

std::string system_error(const char *operation, int error)
{
    return std::string(operation) + ": " + std::strerror(error);
}

std::filesystem::path normalized_save_path(const std::filesystem::path &path, std::string &error)
{
    if (path.empty() || path.filename().empty())
    {
        error = "save destination must name a file";
        return {};
    }
    std::error_code ec;
    const auto absolute = std::filesystem::absolute(path, ec);
    if (ec)
    {
        error = "cannot resolve save destination: " + ec.message();
        return {};
    }
    const auto parent = absolute.parent_path();
    const auto canonicalParent = std::filesystem::canonical(parent, ec);
    if (ec || !std::filesystem::is_directory(canonicalParent))
    {
        error = "save destination directory does not exist: " + parent.string();
        return {};
    }
    return canonicalParent / absolute.filename();
}

std::filesystem::path resolve_existing_destination(const std::filesystem::path &path,
                                                   std::string &error)
{
    std::error_code statusError;
    const auto status = std::filesystem::symlink_status(path, statusError);
    if (statusError && statusError != std::errc::no_such_file_or_directory)
    {
        error = "cannot inspect save destination: " + statusError.message();
        return {};
    }
    if (!std::filesystem::is_symlink(status))
        return path;

    const auto resolved = std::filesystem::canonical(path, statusError);
    if (statusError)
    {
        error = "cannot resolve save destination link: " + statusError.message();
        return {};
    }
    if (!std::filesystem::is_regular_file(resolved, statusError) || statusError)
    {
        error = "save destination link must resolve to a regular file";
        return {};
    }
    return resolved;
}

bool write_all(int fd, std::string_view contents, std::string &error)
{
    std::size_t written = 0;
    while (written < contents.size())
    {
        const ssize_t result = ::write(fd, contents.data() + written, contents.size() - written);
        if (result < 0)
        {
            if (errno == EINTR)
                continue;
            error = system_error("cannot write temporary file", errno);
            return false;
        }
        if (result == 0)
        {
            error = "temporary file write made no progress";
            return false;
        }
        written += static_cast<std::size_t>(result);
    }
    return true;
}
} // namespace

void set_native_file_test_hooks(NativeFileTestHooks hooks)
{
    std::lock_guard lock(testHooksMutex);
    testHooks = std::move(hooks);
}

void clear_native_file_test_hooks()
{
    std::lock_guard lock(testHooksMutex);
    testHooks = {};
}

std::optional<FileIdentity> native_file_identity(const std::filesystem::path &path,
                                                  std::string &error)
{
    struct stat info
    {
    };
    if (::stat(path.c_str(), &info) != 0)
    {
        if (errno == ENOENT || errno == ENOTDIR)
            return std::nullopt;
        error = system_error("cannot inspect destination", errno);
        return std::nullopt;
    }

#if defined(__APPLE__)
    const auto seconds = static_cast<std::int64_t>(info.st_mtimespec.tv_sec);
    const auto nanoseconds = static_cast<std::int64_t>(info.st_mtimespec.tv_nsec);
    const auto changeSeconds = static_cast<std::int64_t>(info.st_ctimespec.tv_sec);
    const auto changeNanoseconds = static_cast<std::int64_t>(info.st_ctimespec.tv_nsec);
#else
    const auto seconds = static_cast<std::int64_t>(info.st_mtim.tv_sec);
    const auto nanoseconds = static_cast<std::int64_t>(info.st_mtim.tv_nsec);
    const auto changeSeconds = static_cast<std::int64_t>(info.st_ctim.tv_sec);
    const auto changeNanoseconds = static_cast<std::int64_t>(info.st_ctim.tv_nsec);
#endif
    const auto maxSeconds = std::numeric_limits<std::int64_t>::max() / 1000000000;
    const auto timestamp = seconds > maxSeconds ? std::numeric_limits<std::int64_t>::max()
                                                : seconds * 1000000000 + nanoseconds;
    const auto changeTimestamp = changeSeconds > maxSeconds ? std::numeric_limits<std::int64_t>::max()
                                                            : changeSeconds * 1000000000 + changeNanoseconds;
    return FileIdentity{static_cast<std::uint64_t>(info.st_dev), static_cast<std::uint64_t>(info.st_ino),
                        static_cast<std::uint64_t>(info.st_size), static_cast<std::uint32_t>(info.st_mode),
                        timestamp, changeTimestamp};
}

AtomicWriteResult native_write_file_atomically(const std::filesystem::path &path,
                                                std::string_view contents,
                                                bool allowReplaceExisting,
                                                std::optional<FileIdentity> expectedDestination,
                                                bool verifyDestinationIdentity)
{
    std::string error;
    auto target = normalized_save_path(path, error);
    if (target.empty())
        return {AtomicWriteStatus::Failed, std::move(error), false};
    const auto requestedTarget = target;
    target = resolve_existing_destination(requestedTarget, error);
    if (target.empty())
        return {AtomicWriteStatus::Failed, std::move(error), false};
    std::string identityError;
    const auto initialIdentity = native_file_identity(target, identityError);
    if (!identityError.empty())
        return {AtomicWriteStatus::Failed, std::move(identityError), false};
    if (verifyDestinationIdentity && initialIdentity != expectedDestination)
        return {AtomicWriteStatus::Conflict, "save destination changed before writing", false};

    const int directoryFd = ::open(target.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directoryFd < 0)
        return {AtomicWriteStatus::Failed, system_error("cannot open save directory", errno), false};

    const auto sequence = temporarySequence.fetch_add(1, std::memory_order_relaxed);
    const auto hooks = current_test_hooks();
    if (hooks.failTemporaryCreate)
    {
        ::close(directoryFd);
        return {AtomicWriteStatus::Failed, "injected temporary-file staging failure", false};
    }
    const std::string targetName = target.filename().string();
    const std::string temporaryName = "." + targetName + ".tmp." + std::to_string(::getpid()) + "." +
                                     std::to_string(sequence);
    const std::string recoveryName = "." + targetName + ".recovery." +
                                     std::to_string(::getpid()) + "." +
                                     std::to_string(sequence);
    mode_t mode = 0666;
    if (initialIdentity)
        mode = static_cast<mode_t>(initialIdentity->mode & 0777u);
    const int fileFd = ::openat(directoryFd, temporaryName.c_str(),
                                O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, mode);
    if (fileFd < 0)
    {
        const int openError = errno;
        ::close(directoryFd);
        return {AtomicWriteStatus::Failed,
                system_error("cannot create same-directory temporary file", openError), false};
    }

    bool temporaryExists = true;
    bool recoveryExists = false;
    auto cleanup_temporary = [&] {
        if (temporaryExists)
        {
            ::unlinkat(directoryFd, temporaryName.c_str(), 0);
            temporaryExists = false;
        }
    };
    auto cleanup_recovery = [&] {
        if (recoveryExists)
        {
            ::unlinkat(directoryFd, recoveryName.c_str(), 0);
            recoveryExists = false;
        }
    };
    if (initialIdentity && ::fchmod(fileFd, mode) != 0)
        error = system_error("cannot preserve destination permissions", errno);
    if (error.empty())
        write_all(fileFd, contents, error);
    if (error.empty() && hooks.failFileFlush)
        error = "injected temporary-file flush failure";
    if (error.empty() && ::fsync(fileFd) != 0)
        error = system_error("cannot flush temporary file", errno);
    const int closeResult = ::close(fileFd);
    if (error.empty() && closeResult != 0)
        error = system_error("cannot close temporary file", errno);
    if (!error.empty())
    {
        cleanup_temporary();
        ::close(directoryFd);
        return {AtomicWriteStatus::Failed, std::move(error), false};
    }

    if (hooks.afterStagingAndFlush)
        hooks.afterStagingAndFlush(target, target.parent_path() / temporaryName);

    std::string lateIdentityError;
    const auto lateResolvedTarget = resolve_existing_destination(requestedTarget, lateIdentityError);
    if (!lateIdentityError.empty())
    {
        cleanup_temporary();
        ::close(directoryFd);
        return {AtomicWriteStatus::Failed,
                "cannot revalidate save destination link: " + lateIdentityError, false};
    }
    if (lateResolvedTarget != target)
    {
        cleanup_temporary();
        ::close(directoryFd);
        return {AtomicWriteStatus::Conflict, "save destination link changed while writing", false};
    }
    const auto currentIdentity = native_file_identity(target, lateIdentityError);
    if (!lateIdentityError.empty())
    {
        cleanup_temporary();
        ::close(directoryFd);
        return {AtomicWriteStatus::Failed, std::move(lateIdentityError), false};
    }
    if (verifyDestinationIdentity && currentIdentity != expectedDestination)
    {
        cleanup_temporary();
        ::close(directoryFd);
        return {AtomicWriteStatus::Conflict, "save destination changed while writing", false};
    }

    if (hooks.beforeCommit)
        hooks.beforeCommit(target);

    const auto preCommitIdentity = native_file_identity(target, lateIdentityError);
    if (!lateIdentityError.empty())
    {
        cleanup_temporary();
        ::close(directoryFd);
        return {AtomicWriteStatus::Failed, std::move(lateIdentityError), false};
    }
    if (verifyDestinationIdentity && preCommitIdentity != expectedDestination)
    {
        cleanup_temporary();
        ::close(directoryFd);
        return {AtomicWriteStatus::Conflict, "save destination changed before commit", false};
    }

    if (allowReplaceExisting && preCommitIdentity)
    {
        if (::linkat(directoryFd, targetName.c_str(), directoryFd, recoveryName.c_str(), 0) != 0)
        {
            const int linkError = errno;
            cleanup_temporary();
            ::close(directoryFd);
            if (linkError == ENOENT && verifyDestinationIdentity)
                return {AtomicWriteStatus::Conflict, "save destination disappeared before recovery staging", false};
            return {AtomicWriteStatus::Failed,
                    system_error("cannot retain prior destination for recovery", linkError), false};
        }
        recoveryExists = true;
        std::string recoveryIdentityError;
        const auto recoveryIdentity = native_file_identity(target.parent_path() / recoveryName,
                                                           recoveryIdentityError);
        const auto linkedDestinationIdentity = native_file_identity(target, recoveryIdentityError);
        if (!recoveryIdentityError.empty() || !recoveryIdentity || !linkedDestinationIdentity ||
            recoveryIdentity->device != linkedDestinationIdentity->device ||
            recoveryIdentity->inode != linkedDestinationIdentity->inode ||
            (verifyDestinationIdentity &&
             (linkedDestinationIdentity->device != expectedDestination->device ||
              linkedDestinationIdentity->inode != expectedDestination->inode ||
              linkedDestinationIdentity->size != expectedDestination->size ||
              linkedDestinationIdentity->mode != expectedDestination->mode ||
              linkedDestinationIdentity->modificationTimeNanoseconds !=
                  expectedDestination->modificationTimeNanoseconds)))
        {
            cleanup_recovery();
            cleanup_temporary();
            ::close(directoryFd);
            if (!recoveryIdentityError.empty())
                return {AtomicWriteStatus::Failed,
                        "cannot verify staged recovery copy: " + recoveryIdentityError, false};
            return {AtomicWriteStatus::Conflict,
                    "save destination changed while staging its recovery copy", false};
        }
    }

    bool destinationReplaced = false;
    bool cleanupWarning = false;
    if (hooks.failReplace)
        error = "injected atomic replacement failure";
    if (!error.empty())
    {
        cleanup_recovery();
        cleanup_temporary();
        ::close(directoryFd);
        return {AtomicWriteStatus::Failed, std::move(error), false};
    }
    if (allowReplaceExisting)
    {
        if (::renameat(directoryFd, temporaryName.c_str(), directoryFd, targetName.c_str()) != 0)
            error = system_error("cannot atomically replace file", errno);
        else
        {
            temporaryExists = false;
            destinationReplaced = true;
        }
    }
    else
    {
        if (::linkat(directoryFd, temporaryName.c_str(), directoryFd, targetName.c_str(), 0) != 0)
            error = errno == EEXIST ? "save destination appeared while writing"
                                    : system_error("cannot atomically create file", errno);
        else
        {
            destinationReplaced = true;
            if (::unlinkat(directoryFd, temporaryName.c_str(), 0) == 0)
                temporaryExists = false;
            else
                cleanupWarning = true;
        }
    }
    if (!destinationReplaced)
    {
        cleanup_recovery();
        cleanup_temporary();
        ::close(directoryFd);
        if (error == "save destination appeared while writing")
            return {AtomicWriteStatus::Conflict, std::move(error), false};
        return {AtomicWriteStatus::Failed, std::move(error), false};
    }

    bool durabilityWarning = cleanupWarning;
    bool directoryDurable = !hooks.failDirectoryFlush && ::fsync(directoryFd) == 0;
    if (!directoryDurable)
    {
        durabilityWarning = true;
        error = hooks.failDirectoryFlush
                    ? "injected directory durability confirmation failure"
                    : system_error("file was replaced but directory durability could not be confirmed", errno);
    }
    if (directoryDurable && recoveryExists)
    {
        if (::unlinkat(directoryFd, recoveryName.c_str(), 0) == 0)
            recoveryExists = false;
        else
        {
            durabilityWarning = true;
            error = system_error("file was committed but its recovery copy could not be removed", errno);
        }
        if (!recoveryExists && ::fsync(directoryFd) != 0)
        {
            durabilityWarning = true;
            if (!error.empty())
                error += "; ";
            error += system_error("file was committed but recovery cleanup durability could not be confirmed",
                                  errno);
        }
    }
    if (recoveryExists)
    {
        durabilityWarning = true;
        if (!error.empty())
            error += "; ";
        error += "prior content retained at " + (target.parent_path() / recoveryName).string();
    }
    ::close(directoryFd);
    if (temporaryExists && ::unlink((target.parent_path() / temporaryName).c_str()) == 0)
        temporaryExists = false;

    return {durabilityWarning ? AtomicWriteStatus::DurabilityWarning : AtomicWriteStatus::Written,
            durabilityWarning ? (error.empty() ? "file was replaced, but directory durability is uncertain" : error)
                              : std::string{},
            true};
}
} // namespace platformator_editor
