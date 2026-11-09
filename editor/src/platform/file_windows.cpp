#include "nativefile.h"
#include "nativefile_testhooks.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <limits>
#include <mutex>
#include <system_error>
#include <utility>

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

std::string system_error(std::string operation, DWORD error)
{
    return std::string(operation) + ": " + std::system_category().message(static_cast<int>(error));
}

std::string path_utf8(const std::filesystem::path &path)
{
    const auto &native = path.native();
    if (native.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return "<path too long to display>";
    const int sourceLength = static_cast<int>(native.size());
    const int byteCount = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, native.data(), sourceLength,
                                                nullptr, 0, nullptr, nullptr);
    if (byteCount <= 0)
        return "<path could not be converted to UTF-8>";
    std::string result(static_cast<std::size_t>(byteCount), '\0');
    if (::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, native.data(), sourceLength,
                              result.data(), byteCount, nullptr, nullptr) != byteCount)
        return "<path could not be converted to UTF-8>";
    return result;
}

class ScopedHandle
{
  public:
    explicit ScopedHandle(HANDLE handle = INVALID_HANDLE_VALUE) noexcept : handle_(handle) {}
    ~ScopedHandle()
    {
        if (valid())
            ::CloseHandle(handle_);
    }
    ScopedHandle(const ScopedHandle &) = delete;
    ScopedHandle &operator=(const ScopedHandle &) = delete;
    ScopedHandle(ScopedHandle &&other) noexcept : handle_(other.release()) {}
    ScopedHandle &operator=(ScopedHandle &&other) noexcept
    {
        if (this != &other)
        {
            reset();
            handle_ = other.release();
        }
        return *this;
    }
    bool valid() const noexcept { return handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr; }
    HANDLE get() const noexcept { return handle_; }
    HANDLE release() noexcept
    {
        const HANDLE result = handle_;
        handle_ = INVALID_HANDLE_VALUE;
        return result;
    }
    bool close_checked() noexcept
    {
        if (!valid())
            return true;
        const HANDLE closing = release();
        return ::CloseHandle(closing) != 0;
    }
    void reset() noexcept
    {
        if (valid())
            ::CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }

  private:
    HANDLE handle_;
};

std::filesystem::path normalized_save_path(const std::filesystem::path &path, std::string &error)
{
    if (path.empty() || path.filename().empty())
    {
        error = "save destination must name a file";
        return {};
    }
    std::error_code ec;
    const auto absolute = std::filesystem::absolute(path, ec).lexically_normal();
    if (ec)
    {
        error = "cannot resolve save destination: " + ec.message();
        return {};
    }
    const auto canonicalParent = std::filesystem::canonical(absolute.parent_path(), ec);
    if (ec || !std::filesystem::is_directory(canonicalParent, ec) || ec)
    {
        error = "save destination directory does not exist: " + absolute.parent_path().string();
        return {};
    }
    return canonicalParent / absolute.filename();
}

std::int64_t filetime_nanoseconds(LONGLONG ticks) noexcept
{
    const auto maximum = std::numeric_limits<std::int64_t>::max();
    if (ticks <= 0)
        return 0;
    if (ticks > maximum / 100)
        return maximum;
    return static_cast<std::int64_t>(ticks * 100);
}

bool path_exists(const std::filesystem::path &path) noexcept
{
    const DWORD attributes = ::GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES;
}

bool write_all(HANDLE file, std::string_view contents, std::string &error)
{
    std::size_t written = 0;
    while (written < contents.size())
    {
        const auto request = static_cast<DWORD>(std::min<std::size_t>(
            contents.size() - written, static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
        DWORD count = 0;
        if (!::WriteFile(file, contents.data() + written, request, &count, nullptr))
        {
            error = system_error("cannot write temporary file", ::GetLastError());
            return false;
        }
        if (count == 0)
        {
            error = "temporary file write made no progress";
            return false;
        }
        written += count;
    }
    return true;
}

bool flush_directory(const std::filesystem::path &directory, DWORD &error)
{
    ScopedHandle handle(::CreateFileW(directory.c_str(), GENERIC_READ | GENERIC_WRITE,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if (!handle.valid())
    {
        error = ::GetLastError();
        return false;
    }
    if (!::FlushFileBuffers(handle.get()))
    {
        error = ::GetLastError();
        return false;
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
    ScopedHandle handle(::CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if (!handle.valid())
    {
        const DWORD code = ::GetLastError();
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND || code == ERROR_INVALID_NAME)
            return std::nullopt;
        error = system_error("cannot inspect destination", code);
        return std::nullopt;
    }

    BY_HANDLE_FILE_INFORMATION legacyInfo{};
    FILE_BASIC_INFO basicInfo{};
    FILE_STANDARD_INFO standardInfo{};
    if (!::GetFileInformationByHandle(handle.get(), &legacyInfo) ||
        !::GetFileInformationByHandleEx(handle.get(), FileBasicInfo, &basicInfo, sizeof(basicInfo)) ||
        !::GetFileInformationByHandleEx(handle.get(), FileStandardInfo, &standardInfo, sizeof(standardInfo)))
    {
        error = system_error("cannot read destination identity", ::GetLastError());
        return std::nullopt;
    }

    const std::uint64_t fileIndex = (static_cast<std::uint64_t>(legacyInfo.nFileIndexHigh) << 32) |
                                    static_cast<std::uint64_t>(legacyInfo.nFileIndexLow);
    return FileIdentity{static_cast<std::uint64_t>(legacyInfo.dwVolumeSerialNumber), fileIndex,
                        static_cast<std::uint64_t>(standardInfo.EndOfFile.QuadPart),
                        static_cast<std::uint32_t>(legacyInfo.dwFileAttributes),
                        filetime_nanoseconds(basicInfo.LastWriteTime.QuadPart),
                        filetime_nanoseconds(basicInfo.ChangeTime.QuadPart)};
}

AtomicWriteResult native_write_file_atomically(const std::filesystem::path &path,
                                                std::string_view contents,
                                                bool allowReplaceExisting,
                                                std::optional<FileIdentity> expectedDestination,
                                                bool verifyDestinationIdentity)
{
    std::string error;
    const auto target = normalized_save_path(path, error);
    if (target.empty())
        return {AtomicWriteStatus::Failed, std::move(error), false};
    std::string identityError;
    const auto initialIdentity = native_file_identity(target, identityError);
    if (!identityError.empty())
        return {AtomicWriteStatus::Failed, std::move(identityError), false};
    if (verifyDestinationIdentity && initialIdentity != expectedDestination)
        return {AtomicWriteStatus::Conflict, "save destination changed before writing", false};

    const auto sequence = temporarySequence.fetch_add(1, std::memory_order_relaxed);
    const auto hooks = current_test_hooks();
    if (hooks.failTemporaryCreate)
        return {AtomicWriteStatus::Failed, "injected temporary-file staging failure", false};
    const auto uniqueSuffix = std::to_wstring(::GetCurrentProcessId()) + L"." + std::to_wstring(sequence);
    const auto temporary = target.parent_path() /
                           (L"." + target.filename().native() + L".tmp." + uniqueSuffix);
    const auto backup = target.parent_path() /
                        (L"." + target.filename().native() + L".bak." + uniqueSuffix);
    ScopedHandle staged(::CreateFileW(temporary.c_str(), GENERIC_WRITE | FILE_READ_ATTRIBUTES, 0,
                                      nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                                      nullptr));
    if (!staged.valid())
        return {AtomicWriteStatus::Failed,
                system_error("cannot create same-directory temporary file", ::GetLastError()), false};

    bool temporaryExists = true;
    auto cleanup_temporary = [&] {
        staged.reset();
        if (temporaryExists)
        {
            ::DeleteFileW(temporary.c_str());
            temporaryExists = false;
        }
    };
    if (!write_all(staged.get(), contents, error))
    {
        cleanup_temporary();
        return {AtomicWriteStatus::Failed, std::move(error), false};
    }
    if (hooks.failFileFlush)
        error = "injected temporary-file flush failure";
    else if (!::FlushFileBuffers(staged.get()))
        error = system_error("cannot flush temporary file", ::GetLastError());
    if (!error.empty())
    {
        cleanup_temporary();
        return {AtomicWriteStatus::Failed, std::move(error), false};
    }
    if (!staged.close_checked())
    {
        const DWORD code = ::GetLastError();
        cleanup_temporary();
        return {AtomicWriteStatus::Failed, system_error("cannot close temporary file", code), false};
    }

    if (hooks.afterStagingAndFlush)
        hooks.afterStagingAndFlush(target, temporary);

    std::string lateIdentityError;
    const auto currentIdentity = native_file_identity(target, lateIdentityError);
    if (!lateIdentityError.empty())
    {
        cleanup_temporary();
        return {AtomicWriteStatus::Failed, std::move(lateIdentityError), false};
    }
    if (verifyDestinationIdentity && currentIdentity != expectedDestination)
    {
        cleanup_temporary();
        return {AtomicWriteStatus::Conflict, "save destination changed while writing", false};
    }
    if (hooks.beforeCommit)
        hooks.beforeCommit(target);

    std::string preCommitIdentityError;
    const auto preCommitIdentity = native_file_identity(target, preCommitIdentityError);
    if (!preCommitIdentityError.empty())
    {
        cleanup_temporary();
        return {AtomicWriteStatus::Failed, std::move(preCommitIdentityError), false};
    }
    if (verifyDestinationIdentity && preCommitIdentity != expectedDestination)
    {
        cleanup_temporary();
        return {AtomicWriteStatus::Conflict, "save destination changed before commit", false};
    }

    if (hooks.failReplace)
    {
        cleanup_temporary();
        return {AtomicWriteStatus::Failed, "injected atomic replacement failure", false};
    }

    bool destinationReplaced = false;
    bool recoveryBackupExists = false;
    bool durabilityWarning = false;
    DWORD durabilityError = ERROR_SUCCESS;
    std::string durabilityMessage;
    if (allowReplaceExisting)
    {
        const DWORD originalAttributes = ::GetFileAttributesW(target.c_str());
        if (originalAttributes == INVALID_FILE_ATTRIBUTES)
        {
            const DWORD code = ::GetLastError();
            cleanup_temporary();
            if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND)
                return {AtomicWriteStatus::Conflict, "save destination disappeared before replacement", false};
            return {AtomicWriteStatus::Failed, system_error("cannot inspect destination attributes", code), false};
        }
        const bool wasReadOnly = (originalAttributes & FILE_ATTRIBUTE_READONLY) != 0;
        if (wasReadOnly && !::SetFileAttributesW(target.c_str(), originalAttributes & ~FILE_ATTRIBUTE_READONLY))
        {
            const DWORD code = ::GetLastError();
            cleanup_temporary();
            return {AtomicWriteStatus::Failed, system_error("cannot prepare destination replacement", code), false};
        }

        if (!::ReplaceFileW(target.c_str(), temporary.c_str(), backup.c_str(), 0, nullptr, nullptr))
        {
            const DWORD replaceError = ::GetLastError();
            const bool hasBackup = path_exists(backup);
            if (hasBackup)
            {
                if (!::MoveFileExW(backup.c_str(), target.c_str(),
                                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                {
                    const DWORD recoveryError = ::GetLastError();
                    if (wasReadOnly)
                        ::SetFileAttributesW(backup.c_str(), originalAttributes);
                    cleanup_temporary();
                    return {AtomicWriteStatus::Failed,
                            system_error("replacement failed; the prior file remains in recovery backup " +
                                             path_utf8(backup), recoveryError), false};
                }
            }
            DWORD attributeRestoreError = ERROR_SUCCESS;
            if (path_exists(target) && !::SetFileAttributesW(target.c_str(), originalAttributes))
                attributeRestoreError = ::GetLastError();
            cleanup_temporary();
            if (attributeRestoreError != ERROR_SUCCESS)
                return {AtomicWriteStatus::Failed,
                        system_error("replacement failed and destination attributes could not be restored",
                                     attributeRestoreError), false};
            if (replaceError == ERROR_FILE_NOT_FOUND || replaceError == ERROR_PATH_NOT_FOUND)
                return {AtomicWriteStatus::Conflict, "save destination changed during replacement", false};
            return {AtomicWriteStatus::Failed, system_error("cannot atomically replace file", replaceError), false};
        }
        temporaryExists = false;
        destinationReplaced = true;
        recoveryBackupExists = path_exists(backup);
        if (!recoveryBackupExists)
        {
            durabilityWarning = true;
            durabilityMessage = "replacement committed but the prior content recovery copy was not retained";
        }
        if (!::SetFileAttributesW(target.c_str(), originalAttributes))
        {
            durabilityWarning = true;
            durabilityError = ::GetLastError();
            if (!durabilityMessage.empty())
                durabilityMessage += "; ";
            durabilityMessage += system_error("file committed but destination attributes could not be restored",
                                              durabilityError);
        }
    }
    else
    {
        if (!::MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH))
        {
            const DWORD code = ::GetLastError();
            cleanup_temporary();
            if (code == ERROR_FILE_EXISTS || code == ERROR_ALREADY_EXISTS)
                return {AtomicWriteStatus::Conflict, "save destination appeared while writing", false};
            return {AtomicWriteStatus::Failed, system_error("cannot atomically create file", code), false};
        }
        temporaryExists = false;
        destinationReplaced = true;
    }

    const bool directoryDurable = !hooks.failDirectoryFlush &&
                                  flush_directory(target.parent_path(), durabilityError);
    if (!directoryDurable)
    {
        durabilityWarning = true;
        const auto directoryMessage = hooks.failDirectoryFlush
                                          ? std::string("injected directory durability confirmation failure")
                                          : system_error("file was replaced but directory durability could not be confirmed",
                                                         durabilityError);
        if (!durabilityMessage.empty())
            durabilityMessage += "; ";
        durabilityMessage += directoryMessage;
    }
    if (recoveryBackupExists && !directoryDurable)
    {
        if (!durabilityMessage.empty())
            durabilityMessage += "; ";
        durabilityMessage += "prior content retained at " + path_utf8(backup);
    }
    else if (recoveryBackupExists)
    {
        if (!::DeleteFileW(backup.c_str()))
        {
            durabilityWarning = true;
            const DWORD code = ::GetLastError();
            if (!durabilityMessage.empty())
                durabilityMessage += "; ";
            durabilityMessage += system_error("file committed but recovery backup could not be removed", code) +
                                 "; prior content retained at " + path_utf8(backup);
        }
        else if (!flush_directory(target.parent_path(), durabilityError))
        {
            durabilityWarning = true;
            if (!durabilityMessage.empty())
                durabilityMessage += "; ";
            durabilityMessage += system_error(
                "file committed but recovery cleanup durability could not be confirmed", durabilityError);
        }
    }
    if (temporaryExists)
        ::DeleteFileW(temporary.c_str());

    if (durabilityWarning)
        return {AtomicWriteStatus::DurabilityWarning, std::move(durabilityMessage), destinationReplaced};
    return {AtomicWriteStatus::Written, {}, destinationReplaced};
}
} // namespace platformator_editor
