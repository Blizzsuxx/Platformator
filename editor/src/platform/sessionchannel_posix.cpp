#include "sessionchannel_posix.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <mutex>
#include <pthread.h>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace platformator_editor
{
namespace
{
constexpr int kFirstChildSourceFd = 6;
constexpr std::array<int, 3> kChildTargetFds{3, 4, 5};
std::atomic<std::uint64_t> sharedMemorySequence{0};

std::string posix_error(const char *operation, int code)
{
    return std::string(operation) + ": " + std::strerror(code);
}

class ScopedFd
{
  public:
    explicit ScopedFd(int fd = -1) noexcept : fd_(fd) {}
    ~ScopedFd() { reset(); }
    ScopedFd(const ScopedFd &) = delete;
    ScopedFd &operator=(const ScopedFd &) = delete;
    ScopedFd(ScopedFd &&other) noexcept : fd_(other.release()) {}
    ScopedFd &operator=(ScopedFd &&other) noexcept
    {
        if (this != &other)
        {
            reset();
            fd_ = other.release();
        }
        return *this;
    }
    int get() const noexcept { return fd_; }
    int release() noexcept
    {
        const int result = fd_;
        fd_ = -1;
        return result;
    }
    void reset(int fd = -1) noexcept
    {
        if (fd_ >= 0)
            ::close(fd_);
        fd_ = fd;
    }

  private:
    int fd_;
};

bool set_descriptor_flag(int fd, int flag, std::string &error)
{
    const int current = ::fcntl(fd, F_GETFD);
    if (current < 0 || ::fcntl(fd, F_SETFD, current | flag) < 0)
    {
        error = posix_error("cannot configure session descriptor", errno);
        return false;
    }
    return true;
}

bool set_status_flag(int fd, int flag, std::string &error)
{
    const int current = ::fcntl(fd, F_GETFL);
    if (current < 0 || ::fcntl(fd, F_SETFL, current | flag) < 0)
    {
        error = posix_error("cannot configure session pipe", errno);
        return false;
    }
    return true;
}

bool move_source_above_reserved(ScopedFd &source, std::string &error);

bool make_pipe(std::array<ScopedFd, 2> &ends, std::string &error)
{
    int descriptors[2]{-1, -1};
    if (::pipe(descriptors) != 0)
    {
        error = posix_error("cannot create session pipe", errno);
        return false;
    }
    ends[0].reset(descriptors[0]);
    ends[1].reset(descriptors[1]);
    for (const auto &end : ends)
        if (!set_descriptor_flag(end.get(), FD_CLOEXEC, error) ||
            !set_status_flag(end.get(), O_NONBLOCK, error))
            return false;
    for (auto &end : ends)
        if (!move_source_above_reserved(end, error))
            return false;
    return true;
}

bool move_source_above_reserved(ScopedFd &source, std::string &error)
{
    if (source.get() >= kFirstChildSourceFd)
        return true;
#if defined(F_DUPFD_CLOEXEC)
    {
        const int duplicate = ::fcntl(source.get(), F_DUPFD_CLOEXEC, kFirstChildSourceFd);
        if (duplicate < 0 && errno != EINVAL)
        {
            error = posix_error("cannot reserve session child descriptor", errno);
            return false;
        }
        if (duplicate >= 0)
        {
            source.reset(duplicate);
            return true;
        }
    }
#endif
    const int duplicate = ::fcntl(source.get(), F_DUPFD, kFirstChildSourceFd);
    if (duplicate < 0)
    {
        error = posix_error("cannot reserve session child descriptor", errno);
        return false;
    }
    ScopedFd replacement(duplicate);
    if (!set_descriptor_flag(replacement.get(), FD_CLOEXEC, error))
        return false;
    source = std::move(replacement);
    return true;
}

SessionChannelResult channel_result(SessionChannelStatus status, std::size_t bytes = 0,
                                    std::string error = {})
{
    return SessionChannelResult{status, bytes, std::move(error)};
}

class PosixPipeChannel final : public SessionChannel
{
  public:
    PosixPipeChannel(int fd, bool readable, bool writable) noexcept
        : fd_(fd), readable_(readable), writable_(writable)
    {
    }
    ~PosixPipeChannel() override { close(); }

    SessionChannelResult read(std::span<std::byte> destination) override
    {
        std::lock_guard lock(mutex_);
        if (cancelled_)
            return channel_result(SessionChannelStatus::Cancelled);
        if (fd_ < 0)
            return channel_result(SessionChannelStatus::Closed);
        if (!readable_)
            return channel_result(SessionChannelStatus::Failed, 0, "session channel is not readable");
        if (destination.empty())
            return channel_result(SessionChannelStatus::Progress);
        for (;;)
        {
            const ssize_t count = ::read(fd_, destination.data(), destination.size());
            if (count > 0)
                return channel_result(SessionChannelStatus::Progress, static_cast<std::size_t>(count));
            if (count == 0)
                return channel_result(SessionChannelStatus::Closed);
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return channel_result(SessionChannelStatus::WouldBlock);
            return channel_result(SessionChannelStatus::Failed, 0, posix_error("session pipe read failed", errno));
        }
    }

    SessionChannelResult write(std::span<const std::byte> source) override
    {
        std::lock_guard lock(mutex_);
        if (cancelled_)
            return channel_result(SessionChannelStatus::Cancelled);
        if (fd_ < 0)
            return channel_result(SessionChannelStatus::Closed);
        if (!writable_)
            return channel_result(SessionChannelStatus::Failed, 0, "session channel is not writable");
        if (source.empty())
            return channel_result(SessionChannelStatus::Progress);

        sigset_t blocked{};
        sigset_t previous{};
        sigemptyset(&blocked);
        sigaddset(&blocked, SIGPIPE);
        const int maskError = ::pthread_sigmask(SIG_BLOCK, &blocked, &previous);
        if (maskError != 0)
            return channel_result(SessionChannelStatus::Failed, 0,
                                  posix_error("cannot block SIGPIPE for session write", maskError));
        sigset_t pending{};
        const bool wasPending = ::sigpending(&pending) == 0 && sigismember(&pending, SIGPIPE) == 1;

        ssize_t count = -1;
        int writeError = 0;
        for (;;)
        {
            count = ::write(fd_, source.data(), source.size());
            if (count >= 0 || errno != EINTR)
            {
                if (count < 0)
                    writeError = errno;
                break;
            }
        }
        if (writeError == EPIPE && !wasPending)
        {
            int signalNumber = 0;
            ::sigwait(&blocked, &signalNumber);
        }
        const int restoreError = ::pthread_sigmask(SIG_SETMASK, &previous, nullptr);
        if (restoreError != 0)
            return channel_result(SessionChannelStatus::Failed, 0,
                                  posix_error("cannot restore signal mask after session write", restoreError));
        if (count >= 0)
            return channel_result(SessionChannelStatus::Progress, static_cast<std::size_t>(count));
        if (writeError == EAGAIN || writeError == EWOULDBLOCK)
            return channel_result(SessionChannelStatus::WouldBlock);
        if (writeError == EPIPE)
            return channel_result(SessionChannelStatus::Closed);
        return channel_result(SessionChannelStatus::Failed, 0,
                              posix_error("session pipe write failed", writeError));
    }

    void cancel() noexcept override
    {
        std::lock_guard lock(mutex_);
        cancelled_ = true;
        if (fd_ >= 0)
        {
            ::close(fd_);
            fd_ = -1;
        }
    }

    void close() noexcept override
    {
        std::lock_guard lock(mutex_);
        if (fd_ >= 0)
        {
            ::close(fd_);
            fd_ = -1;
        }
    }

  private:
    std::mutex mutex_;
    int fd_ = -1;
    bool readable_ = false;
    bool writable_ = false;
    bool cancelled_ = false;
};

class PosixMapping final : public SessionMapping
{
  public:
    PosixMapping(void *address, std::size_t size, int fd) noexcept
        : address_(address), size_(size), fd_(fd)
    {
    }
    ~PosixMapping() override { close(); }

    std::span<std::byte> bytes() noexcept override
    {
        return address_ == MAP_FAILED || address_ == nullptr
                   ? std::span<std::byte>{}
                   : std::span<std::byte>(static_cast<std::byte *>(address_), size_);
    }
    std::size_t size() const noexcept override { return address_ == MAP_FAILED ? 0 : size_; }

    void close() noexcept override
    {
        if (address_ != MAP_FAILED && address_ != nullptr)
        {
            ::munmap(address_, size_);
            address_ = MAP_FAILED;
            size_ = 0;
        }
        if (fd_ >= 0)
        {
            ::close(fd_);
            fd_ = -1;
        }
    }

  private:
    void *address_ = MAP_FAILED;
    std::size_t size_ = 0;
    int fd_ = -1;
};

class PosixSessionChannelProvider final : public SessionChannelProvider
{
  public:
    std::unique_ptr<SessionChannelPair> create_pair(std::size_t mappingBytes,
                                                    std::string &error) override
    {
        error.clear();
        if (mappingBytes == 0 || mappingBytes > session_frame_mapping_limit)
        {
            error = "session frame mapping size must be between 1 byte and 1,843,200 bytes";
            return nullptr;
        }

        std::array<ScopedFd, 2> commandPipe;
        std::array<ScopedFd, 2> eventPipe;
        if (!make_pipe(commandPipe, error) || !make_pipe(eventPipe, error))
            return nullptr;

        if (!move_source_above_reserved(commandPipe[0], error) ||
            !move_source_above_reserved(eventPipe[1], error))
            return nullptr;

        const auto sequence = sharedMemorySequence.fetch_add(1, std::memory_order_relaxed);
        const std::string name = "/platformator-editor-session-" +
                                 std::to_string(static_cast<long long>(::getpid())) + "-" +
                                 std::to_string(sequence);
        ScopedFd sharedFd(::shm_open(name.c_str(), O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR));
        if (sharedFd.get() < 0)
        {
            error = posix_error("cannot create anonymous session mapping", errno);
            return nullptr;
        }
        if (!set_descriptor_flag(sharedFd.get(), FD_CLOEXEC, error))
        {
            ::shm_unlink(name.c_str());
            return nullptr;
        }
        if (::shm_unlink(name.c_str()) != 0)
        {
            error = posix_error("cannot unlink session mapping name", errno);
            ::shm_unlink(name.c_str());
            return nullptr;
        }
        if (::ftruncate(sharedFd.get(), static_cast<off_t>(mappingBytes)) != 0)
        {
            error = posix_error("cannot size session mapping", errno);
            return nullptr;
        }
        void *address = ::mmap(nullptr, mappingBytes, PROT_READ | PROT_WRITE, MAP_SHARED,
                               sharedFd.get(), 0);
        if (address == MAP_FAILED)
        {
            error = posix_error("cannot map session frame memory", errno);
            return nullptr;
        }

        ScopedFd childMappingFd(::fcntl(sharedFd.get(), F_DUPFD, kFirstChildSourceFd));
        if (childMappingFd.get() < 0 || !set_descriptor_flag(childMappingFd.get(), FD_CLOEXEC, error))
        {
            const int mapError = childMappingFd.get() < 0 ? errno : 0;
            ::munmap(address, mappingBytes);
            if (mapError != 0)
                error = posix_error("cannot reserve child mapping descriptor", mapError);
            return nullptr;
        }

        auto pair = std::make_unique<SessionChannelPair>();
        pair->closeNativeHandle = [](std::uintptr_t handle) { ::close(static_cast<int>(handle)); };
        pair->commands = std::make_unique<PosixPipeChannel>(commandPipe[1].release(), false, true);
        pair->events = std::make_unique<PosixPipeChannel>(eventPipe[0].release(), true, false);
        pair->frameMapping = std::make_unique<PosixMapping>(address, mappingBytes, sharedFd.release());
        pair->childEndpoints = {
            {"--editor-session-command-fd", "3", static_cast<std::uintptr_t>(commandPipe[0].release())},
            {"--editor-session-event-fd", "4", static_cast<std::uintptr_t>(eventPipe[1].release())},
            {"--editor-session-frame-fd", "5", static_cast<std::uintptr_t>(childMappingFd.release())}};
        return pair;
    }
};
} // namespace

std::unique_ptr<SessionChannelProvider> make_posix_session_channel_provider()
{
    return std::make_unique<PosixSessionChannelProvider>();
}

bool add_posix_session_spawn_actions(const SessionChannelPair &pair,
                                    posix_spawn_file_actions_t &actions,
                                    std::string &error)
{
    if (pair.childEndpoints.size() != kChildTargetFds.size())
    {
        error = "session launch requires exactly three inherited endpoints";
        return false;
    }
    for (std::size_t index = 0; index < pair.childEndpoints.size(); ++index)
    {
        const auto sourceValue = pair.childEndpoints[index].nativeHandle;
        if (sourceValue > static_cast<std::uintptr_t>(std::numeric_limits<int>::max()) ||
            sourceValue < static_cast<std::uintptr_t>(kFirstChildSourceFd))
        {
            error = "session source descriptor is outside the reserved-safe range";
            return false;
        }
        const int source = static_cast<int>(sourceValue);
        const bool duplicateSource = std::any_of(
            pair.childEndpoints.begin(),
            pair.childEndpoints.begin() + static_cast<std::ptrdiff_t>(index),
            [&](const InheritedSessionEndpoint &endpoint) {
                return endpoint.nativeHandle == sourceValue;
            });
        if (duplicateSource)
        {
            error = "session source descriptors must be distinct";
            return false;
        }
        const int result = ::posix_spawn_file_actions_adddup2(&actions, source, kChildTargetFds[index]);
        if (result != 0)
        {
            error = posix_error("cannot map session descriptor into child", result);
            return false;
        }
        const int closeResult = ::posix_spawn_file_actions_addclose(&actions, source);
        if (closeResult != 0)
        {
            error = posix_error("cannot close parent session source in child", closeResult);
            return false;
        }
    }
    return true;
}

bool mark_session_child_descriptors_close_on_exec(std::string &error)
{
    error.clear();
    for (const int descriptor : kChildTargetFds)
        if (!set_descriptor_flag(descriptor, FD_CLOEXEC, error))
            return false;
    return true;
}
} // namespace platformator_editor
