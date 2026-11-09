#include "sessionchannel_windows.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <limits>
#include <mutex>
#include <string>
#include <system_error>
#include <stdexcept>
#include <utility>
#include <vector>

namespace platformator_editor
{
namespace
{
constexpr std::size_t maximumWriteBytes = 1024U * 1024U;
constexpr std::size_t readBufferBytes = 64U * 1024U;
std::atomic<std::uint64_t> pipeSequence{0};

std::string windows_error(const char *operation, DWORD code)
{
    return std::string(operation) + ": " + std::system_category().message(static_cast<int>(code));
}

class ScopedHandle
{
  public:
    explicit ScopedHandle(HANDLE value = INVALID_HANDLE_VALUE) noexcept : value_(value) {}
    ~ScopedHandle() { reset(); }
    ScopedHandle(const ScopedHandle &) = delete;
    ScopedHandle &operator=(const ScopedHandle &) = delete;
    ScopedHandle(ScopedHandle &&other) noexcept : value_(other.release()) {}
    ScopedHandle &operator=(ScopedHandle &&other) noexcept
    {
        if (this != &other)
        {
            reset();
            value_ = other.release();
        }
        return *this;
    }
    bool valid() const noexcept { return value_ != INVALID_HANDLE_VALUE && value_ != nullptr; }
    HANDLE get() const noexcept { return value_; }
    HANDLE release() noexcept
    {
        const HANDLE result = value_;
        value_ = INVALID_HANDLE_VALUE;
        return result;
    }
    void reset(HANDLE value = INVALID_HANDLE_VALUE) noexcept
    {
        if (valid())
            ::CloseHandle(value_);
        value_ = value;
    }

  private:
    HANDLE value_;
};

class ScopedView
{
  public:
    explicit ScopedView(void *address = nullptr) noexcept : address_(address) {}
    ~ScopedView()
    {
        if (address_ != nullptr)
            ::UnmapViewOfFile(address_);
    }
    ScopedView(const ScopedView &) = delete;
    ScopedView &operator=(const ScopedView &) = delete;
    void *get() const noexcept { return address_; }
    void *release() noexcept
    {
        void *result = address_;
        address_ = nullptr;
        return result;
    }

  private:
    void *address_ = nullptr;
};

SessionChannelResult result(SessionChannelStatus status, std::size_t bytes = 0,
                            std::string error = {})
{
    return {status, bytes, std::move(error)};
}

SessionChannelStatus pipe_error_status(DWORD code) noexcept
{
    if (code == ERROR_BROKEN_PIPE || code == ERROR_PIPE_NOT_CONNECTED ||
        code == ERROR_NO_DATA || code == ERROR_HANDLE_EOF)
        return SessionChannelStatus::Closed;
    if (code == ERROR_OPERATION_ABORTED)
        return SessionChannelStatus::Cancelled;
    return SessionChannelStatus::Failed;
}

class WindowsPipeChannel final : public SessionChannel
{
  public:
    WindowsPipeChannel(ScopedHandle pipe, bool readable, bool writable)
        : pipe_(std::move(pipe)), readable_(readable), writable_(writable),
          event_(::CreateEventW(nullptr, TRUE, FALSE, nullptr))
    {
        if (!pipe_.valid() || !event_.valid())
            throw std::runtime_error(windows_error("could not initialize overlapped session pipe",
                event_.valid() ? ERROR_INVALID_HANDLE : ::GetLastError()));
        overlapped_.hEvent = event_.get();
    }

    ~WindowsPipeChannel() override { close(); }

    SessionChannelResult read(std::span<std::byte> destination) override
    {
        std::lock_guard lock(mutex_);
        if (cancelled_)
            return result(SessionChannelStatus::Cancelled);
        if (closed_ || !pipe_.valid())
            return result(SessionChannelStatus::Closed);
        if (!readable_)
            return result(SessionChannelStatus::Failed, 0, "session channel is not readable");
        if (destination.empty())
            return result(SessionChannelStatus::Progress);

        if (readOffset_ < readCount_)
            return copy_read_buffer(destination);
        readOffset_ = readCount_ = 0;

        if (pending_)
        {
            DWORD transferred = 0;
            if (!::GetOverlappedResult(pipe_.get(), &overlapped_, &transferred, FALSE))
            {
                const DWORD code = ::GetLastError();
                if (code == ERROR_IO_INCOMPLETE)
                    return result(SessionChannelStatus::WouldBlock);
                pending_ = false;
                if (pipe_error_status(code) == SessionChannelStatus::Closed)
                    closed_ = true;
                return result(pipe_error_status(code), 0,
                    pipe_error_status(code) == SessionChannelStatus::Failed
                        ? windows_error("session pipe read failed", code) : std::string{});
            }
            pending_ = false;
            if (transferred == 0)
            {
                closed_ = true;
                return result(SessionChannelStatus::Closed);
            }
            readCount_ = transferred;
            return copy_read_buffer(destination);
        }

        ::ResetEvent(event_.get());
        overlapped_ = {};
        overlapped_.hEvent = event_.get();
        DWORD transferred = 0;
        if (::ReadFile(pipe_.get(), readBuffer_.data(), static_cast<DWORD>(readBuffer_.size()),
                       &transferred, &overlapped_))
        {
            if (transferred == 0)
            {
                closed_ = true;
                return result(SessionChannelStatus::Closed);
            }
            readCount_ = transferred;
            return copy_read_buffer(destination);
        }
        const DWORD code = ::GetLastError();
        if (code == ERROR_IO_PENDING)
        {
            pending_ = true;
            return result(SessionChannelStatus::WouldBlock);
        }
        if (pipe_error_status(code) == SessionChannelStatus::Closed)
            closed_ = true;
        return result(pipe_error_status(code), 0,
            pipe_error_status(code) == SessionChannelStatus::Failed
                ? windows_error("session pipe read failed", code) : std::string{});
    }

    SessionChannelResult write(std::span<const std::byte> source) override
    {
        std::lock_guard lock(mutex_);
        if (cancelled_)
            return result(SessionChannelStatus::Cancelled);
        if (closed_ || !pipe_.valid())
            return result(SessionChannelStatus::Closed);
        if (!writable_)
            return result(SessionChannelStatus::Failed, 0, "session channel is not writable");
        if (source.empty())
            return result(SessionChannelStatus::Progress);

        if (pending_)
        {
            DWORD transferred = 0;
            if (!::GetOverlappedResult(pipe_.get(), &overlapped_, &transferred, FALSE))
            {
                const DWORD code = ::GetLastError();
                if (code == ERROR_IO_INCOMPLETE)
                    return result(SessionChannelStatus::WouldBlock);
                pending_ = false;
                writeBuffer_.clear();
                if (pipe_error_status(code) == SessionChannelStatus::Closed)
                    closed_ = true;
                return result(pipe_error_status(code), 0,
                    pipe_error_status(code) == SessionChannelStatus::Failed
                        ? windows_error("session pipe write failed", code) : std::string{});
            }
            pending_ = false;
            const bool sameWrite = transferred <= writeBuffer_.size() &&
                source.size() >= transferred &&
                std::equal(writeBuffer_.begin(), writeBuffer_.begin() + transferred, source.begin());
            writeBuffer_.clear();
            if (!sameWrite)
                return result(SessionChannelStatus::Failed, 0,
                              "session write buffer changed before overlapped completion");
            if (transferred == 0)
                closed_ = true;
            return result(transferred == 0 ? SessionChannelStatus::Closed
                                           : SessionChannelStatus::Progress,
                          transferred);
        }

        if (source.size() > maximumWriteBytes)
            return result(SessionChannelStatus::Failed, 0, "session write exceeds the 1 MiB channel limit");
        try
        {
            writeBuffer_.assign(source.begin(), source.end());
        }
        catch (const std::exception &error)
        {
            return result(SessionChannelStatus::Failed, 0,
                          std::string("could not stage session pipe write: ") + error.what());
        }
        ::ResetEvent(event_.get());
        overlapped_ = {};
        overlapped_.hEvent = event_.get();
        DWORD transferred = 0;
        if (::WriteFile(pipe_.get(), writeBuffer_.data(), static_cast<DWORD>(writeBuffer_.size()),
                        &transferred, &overlapped_))
        {
            writeBuffer_.clear();
            if (transferred == 0)
                closed_ = true;
            return result(transferred == 0 ? SessionChannelStatus::Closed
                                           : SessionChannelStatus::Progress,
                          transferred);
        }
        const DWORD code = ::GetLastError();
        if (code == ERROR_IO_PENDING)
        {
            pending_ = true;
            return result(SessionChannelStatus::WouldBlock);
        }
        writeBuffer_.clear();
        if (pipe_error_status(code) == SessionChannelStatus::Closed)
            closed_ = true;
        return result(pipe_error_status(code), 0,
            pipe_error_status(code) == SessionChannelStatus::Failed
                ? windows_error("session pipe write failed", code) : std::string{});
    }

    void cancel() noexcept override { shutdown(true); }
    void close() noexcept override { shutdown(false); }

  private:
    SessionChannelResult copy_read_buffer(std::span<std::byte> destination)
    {
        const std::size_t count = std::min(destination.size(), readCount_ - readOffset_);
        std::copy_n(readBuffer_.begin() + static_cast<std::ptrdiff_t>(readOffset_), count,
                    destination.begin());
        readOffset_ += count;
        if (readOffset_ == readCount_)
            readOffset_ = readCount_ = 0;
        return result(SessionChannelStatus::Progress, count);
    }

    void shutdown(bool cancelled) noexcept
    {
        std::lock_guard lock(mutex_);
        if (cancelled)
            cancelled_ = true;
        else
            closed_ = true;
        if (pending_ && pipe_.valid())
        {
            const bool cancelled = ::CancelIoEx(pipe_.get(), &overlapped_) != 0;
            const DWORD cancelError = cancelled ? ERROR_SUCCESS : ::GetLastError();
            const bool alreadyComplete = !cancelled && cancelError == ERROR_NOT_FOUND;
            if (!cancelled && !alreadyComplete)
            {
                // Closing the pipe forces the outstanding operation to finish before
                // its OVERLAPPED storage or event is released.
                pipe_.reset();
            }
            ::WaitForSingleObject(event_.get(), INFINITE);
            if (pipe_.valid())
            {
                DWORD ignored = 0;
                ::GetOverlappedResult(pipe_.get(), &overlapped_, &ignored, FALSE);
            }
            pending_ = false;
        }
        pipe_.reset();
        event_.reset();
        readOffset_ = readCount_ = 0;
        writeBuffer_.clear();
    }

    ScopedHandle pipe_;
    bool readable_ = false;
    bool writable_ = false;
    ScopedHandle event_;
    OVERLAPPED overlapped_{};
    std::array<std::byte, readBufferBytes> readBuffer_{};
    std::vector<std::byte> writeBuffer_;
    std::size_t readOffset_ = 0;
    std::size_t readCount_ = 0;
    bool pending_ = false;
    bool cancelled_ = false;
    bool closed_ = false;
    std::mutex mutex_;
};

class WindowsMapping final : public SessionMapping
{
  public:
    WindowsMapping(ScopedHandle mapping, void *address, std::size_t size) noexcept
        : mapping_(std::move(mapping)), address_(address), size_(size)
    {
    }
    ~WindowsMapping() override { close(); }

    std::span<std::byte> bytes() noexcept override
    {
        return address_ == nullptr ? std::span<std::byte>{}
                                   : std::span<std::byte>(static_cast<std::byte *>(address_), size_);
    }
    std::size_t size() const noexcept override { return address_ == nullptr ? 0 : size_; }
    void close() noexcept override
    {
        if (address_ != nullptr)
        {
            ::UnmapViewOfFile(address_);
            address_ = nullptr;
        }
        size_ = 0;
        mapping_.reset();
    }

  private:
    ScopedHandle mapping_;
    void *address_ = nullptr;
    std::size_t size_ = 0;
};

bool connect_named_pipe(HANDLE server, HANDLE client, std::string &error)
{
    ScopedHandle event(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!event.valid())
    {
        error = windows_error("could not create named-pipe connection event", ::GetLastError());
        return false;
    }
    OVERLAPPED overlapped{};
    overlapped.hEvent = event.get();
    if (::ConnectNamedPipe(server, &overlapped))
        return true;
    const DWORD code = ::GetLastError();
    if (code == ERROR_PIPE_CONNECTED)
        return true;
    if (code != ERROR_IO_PENDING)
    {
        error = windows_error("could not connect private session pipe", code);
        return false;
    }
    const DWORD wait = ::WaitForSingleObject(event.get(), 5000);
    if (wait != WAIT_OBJECT_0)
    {
        ::CancelIoEx(server, &overlapped);
        ::WaitForSingleObject(event.get(), INFINITE);
        DWORD ignored = 0;
        ::GetOverlappedResult(server, &overlapped, &ignored, FALSE);
        error = wait == WAIT_TIMEOUT ? "timed out connecting private session pipe"
                                     : windows_error("could not wait for private session pipe", ::GetLastError());
        return false;
    }
    DWORD ignored = 0;
    if (!::GetOverlappedResult(server, &overlapped, &ignored, FALSE))
    {
        error = windows_error("could not complete private session pipe connection", ::GetLastError());
        return false;
    }
    (void)client;
    return true;
}

bool make_pipe_pair(const std::wstring &name, bool parentReads, ScopedHandle &server,
                    ScopedHandle &child, std::string &error)
{
    SECURITY_ATTRIBUTES childSecurity{};
    childSecurity.nLength = sizeof(childSecurity);
    childSecurity.bInheritHandle = TRUE;
    const DWORD access = (parentReads ? PIPE_ACCESS_INBOUND : PIPE_ACCESS_OUTBOUND) |
                         FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE;
    server.reset(::CreateNamedPipeW(name.c_str(), access,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1, static_cast<DWORD>(readBufferBytes), static_cast<DWORD>(readBufferBytes), 0, nullptr));
    if (!server.valid())
    {
        error = windows_error("could not create private session pipe", ::GetLastError());
        return false;
    }
    child.reset(::CreateFileW(name.c_str(), parentReads ? GENERIC_WRITE : GENERIC_READ,
                              0, &childSecurity, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!child.valid())
    {
        error = windows_error("could not create inherited session endpoint", ::GetLastError());
        return false;
    }
    if (!connect_named_pipe(server.get(), child.get(), error))
        return false;
    return true;
}

class WindowsSessionChannelProvider final : public SessionChannelProvider
{
  public:
    std::unique_ptr<SessionChannelPair> create_pair(std::size_t mappingBytes,
                                                    std::string &error) override
    {
        error.clear();
        if (mappingBytes == 0 || mappingBytes > session_frame_mapping_limit ||
            mappingBytes > std::numeric_limits<DWORD>::max())
        {
            error = "session frame mapping size must be between 1 byte and 1,843,200 bytes";
            return nullptr;
        }

        const auto sequence = pipeSequence.fetch_add(1, std::memory_order_relaxed);
        const std::wstring prefix = L"\\\\.\\pipe\\Platformator.EditorSession." +
            std::to_wstring(::GetCurrentProcessId()) + L"." + std::to_wstring(sequence);
        ScopedHandle commandServer;
        ScopedHandle commandChild;
        ScopedHandle eventServer;
        ScopedHandle eventChild;
        if (!make_pipe_pair(prefix + L".commands", false, commandServer, commandChild, error) ||
            !make_pipe_pair(prefix + L".events", true, eventServer, eventChild, error))
            return nullptr;

        const DWORD high = static_cast<DWORD>(static_cast<std::uint64_t>(mappingBytes) >> 32);
        const DWORD low = static_cast<DWORD>(mappingBytes & 0xffffffffu);
        ScopedHandle mapping(::CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                                  high, low, nullptr));
        if (!mapping.valid())
        {
            error = windows_error("could not create unnamed session mapping", ::GetLastError());
            return nullptr;
        }
        ScopedView address(::MapViewOfFile(mapping.get(), FILE_MAP_ALL_ACCESS, 0, 0, mappingBytes));
        if (address.get() == nullptr)
        {
            error = windows_error("could not map session frame memory", ::GetLastError());
            return nullptr;
        }

        ScopedHandle childMapping;
        HANDLE childMappingHandle = INVALID_HANDLE_VALUE;
        if (!::DuplicateHandle(::GetCurrentProcess(), mapping.get(), ::GetCurrentProcess(),
                               &childMappingHandle, 0, TRUE, DUPLICATE_SAME_ACCESS))
        {
            const DWORD code = ::GetLastError();
            error = windows_error("could not create inheritable session mapping handle", code);
            return nullptr;
        }
        childMapping.reset(childMappingHandle);

        auto pair = std::make_unique<SessionChannelPair>();
        pair->closeNativeHandle = [](std::uintptr_t handle) {
            if (handle != 0)
                ::CloseHandle(reinterpret_cast<HANDLE>(handle));
        };
        pair->commands = std::make_unique<WindowsPipeChannel>(std::move(commandServer), false, true);
        pair->events = std::make_unique<WindowsPipeChannel>(std::move(eventServer), true, false);
        auto frameMapping = std::make_unique<WindowsMapping>(std::move(mapping), address.get(), mappingBytes);
        address.release();
        pair->frameMapping = std::move(frameMapping);
        const auto append_endpoint = [&](const char *argument, std::string value, ScopedHandle &handle) {
            const auto native = reinterpret_cast<std::uintptr_t>(handle.get());
            pair->childEndpoints.push_back({argument, std::move(value), native});
            handle.release();
        };
        append_endpoint("--editor-session-command-handle",
                        std::to_string(reinterpret_cast<std::uintptr_t>(commandChild.get())), commandChild);
        append_endpoint("--editor-session-event-handle",
                        std::to_string(reinterpret_cast<std::uintptr_t>(eventChild.get())), eventChild);
        append_endpoint("--editor-session-frame-handle",
                        std::to_string(reinterpret_cast<std::uintptr_t>(childMapping.get())), childMapping);
        return pair;
    }
};
} // namespace

std::unique_ptr<SessionChannelProvider> make_windows_session_channel_provider()
{
    return std::make_unique<WindowsSessionChannelProvider>();
}
} // namespace platformator_editor
