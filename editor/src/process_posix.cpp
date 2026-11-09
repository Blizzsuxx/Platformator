#include "process_internal.h"
#include "platform/nativeprocess_testhooks.h"
#include "platform/sessionchannel_posix.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <thread>
#include <sys/wait.h>
#include <unistd.h>

#include <spawn.h>

extern char **environ;

namespace platformator_editor
{
namespace
{
struct PosixProcessState
{
    pid_t pid = -1;
    pid_t processGroup = -1;
    int standardOutputFd = -1;
    int standardErrorFd = -1;
    ProcessOutputMode outputMode = ProcessOutputMode::Merged;
    std::uint64_t generation = 0;
    ProcessService::OutputCallback output;
    ProcessService::CompletionCallback completion;
    bool childReaped = false;
    int waitStatus = 0;
    bool stopRequested = false;
    bool killSent = false;
    std::chrono::steady_clock::time_point killDeadline{};
    std::string ioError;
};
std::atomic<bool> forceForkFallbackForTesting{false};
} // namespace

namespace
{
constexpr std::size_t kReadChunkBytes = 8192;
constexpr std::size_t kMaxReadBytesPerPoll = 64 * 1024;
constexpr auto kStopGrace = std::chrono::seconds(2);

struct PipePair
{
    int readFd = -1;
    int writeFd = -1;
};

void close_fd(int &fd)
{
    if (fd >= 0)
    {
        ::close(fd);
        fd = -1;
    }
}

int duplicate_above_standard_streams(int &fd)
{
    if (fd >= STDERR_FILENO + 1)
        return 0;
    const int replacement = ::fcntl(fd, F_DUPFD, STDERR_FILENO + 1);
    if (replacement < 0)
        return errno;
    ::close(fd);
    fd = replacement;
    return 0;
}

int set_close_on_exec(int fd)
{
    const int flags = ::fcntl(fd, F_GETFD);
    if (flags < 0 || ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0)
        return errno;
    return 0;
}

int duplicate_above_session_targets(int &fd)
{
    constexpr int firstAvailable = 6;
    if (fd >= firstAvailable)
        return 0;
    const int replacement = ::fcntl(fd, F_DUPFD, firstAvailable);
    if (replacement < 0)
        return errno;
    const int flagError = set_close_on_exec(replacement);
    if (flagError != 0)
    {
        ::close(replacement);
        return flagError;
    }
    ::close(fd);
    fd = replacement;
    return 0;
}

int set_nonblocking(int fd)
{
    const int flags = ::fcntl(fd, F_GETFL);
    if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        return errno;
    return 0;
}

int create_pipe(PipePair &pipe)
{
    int fds[2]{-1, -1};
    if (::pipe(fds) != 0)
        return errno;
    pipe.readFd = fds[0];
    pipe.writeFd = fds[1];
    for (int *fd : {&pipe.readFd, &pipe.writeFd})
    {
        int error = duplicate_above_standard_streams(*fd);
        if (error == 0)
            error = set_close_on_exec(*fd);
        if (error != 0)
        {
            close_fd(pipe.readFd);
            close_fd(pipe.writeFd);
            return error;
        }
    }
    const int nonblockingError = set_nonblocking(pipe.readFd);
    if (nonblockingError != 0)
    {
        close_fd(pipe.readFd);
        close_fd(pipe.writeFd);
        return nonblockingError;
    }
    return 0;
}

int reserve_session_spawn_descriptors(PipePair &pipe)
{
    // The fork fallback closes these descriptors after mapping channels to fd 3/4/5.
    // Keep stdio pipes outside that reserved range so it cannot close the new mappings.
    for (int *fd : {&pipe.readFd, &pipe.writeFd})
    {
        const int error = duplicate_above_session_targets(*fd);
        if (error != 0)
            return error;
    }
    return 0;
}

void close_pipe(PipePair &pipe)
{
    close_fd(pipe.readFd);
    close_fd(pipe.writeFd);
}

std::vector<char *> make_argv(std::vector<std::string> &arguments)
{
    std::vector<char *> result;
    result.reserve(arguments.size() + 1);
    for (std::string &argument : arguments)
        result.push_back(argument.data());
    result.push_back(nullptr);
    return result;
}

void report_spawn_failure(std::uint64_t generation, ProcessService::CompletionCallback &completion,
                          int error)
{
    if (!completion)
        return;
    ProcessCompletion result;
    result.generation = generation;
    result.error = std::strerror(error);
    completion(std::move(result));
}

#if defined(PLATFORMATOR_HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR) || \
    defined(PLATFORMATOR_HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR_NP)
int spawn_process_with_posix_spawn(pid_t &pid, ProcessRequest &request,
                                   PipePair &outPipe, PipePair &errPipe)
{
    posix_spawn_file_actions_t actions;
    int error = ::posix_spawn_file_actions_init(&actions);
    if (error != 0)
        return error;
    posix_spawnattr_t attributes;
    bool attributesInitialized = false;
    auto add_action = [&](int result) {
        if (error == 0 && result != 0)
            error = result;
    };

    add_action(::posix_spawn_file_actions_adddup2(&actions, outPipe.writeFd, STDOUT_FILENO));
    if (request.outputMode == ProcessOutputMode::Merged)
        add_action(::posix_spawn_file_actions_adddup2(&actions, outPipe.writeFd, STDERR_FILENO));
    else
        add_action(::posix_spawn_file_actions_adddup2(&actions, errPipe.writeFd, STDERR_FILENO));

    for (int fd : {outPipe.readFd, outPipe.writeFd, errPipe.readFd, errPipe.writeFd})
        if (fd >= 0)
            add_action(::posix_spawn_file_actions_addclose(&actions, fd));

    if (error == 0 && request.sessionChannels != nullptr)
    {
        std::string sessionError;
        if (!add_posix_session_spawn_actions(*request.sessionChannels, actions, sessionError))
            error = EINVAL;
    }

    if (error == 0)
    {
#if defined(PLATFORMATOR_HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR)
        add_action(::posix_spawn_file_actions_addchdir(&actions, request.workingDirectory.c_str()));
#else
        add_action(::posix_spawn_file_actions_addchdir_np(&actions, request.workingDirectory.c_str()));
#endif
    }

    if (error == 0)
    {
        error = ::posix_spawnattr_init(&attributes);
        attributesInitialized = error == 0;
    }
    if (error == 0)
        error = ::posix_spawnattr_setpgroup(&attributes, 0);
    if (error == 0)
        error = ::posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);

    if (error == 0)
    {
        auto arguments = make_argv(request.argv);
        error = ::posix_spawnp(&pid, arguments[0], &actions, &attributes, arguments.data(), environ);
    }
    if (attributesInitialized)
        ::posix_spawnattr_destroy(&attributes);
    ::posix_spawn_file_actions_destroy(&actions);
    return error;
}
#endif

std::filesystem::path find_executable(const std::string &program,
                                      const std::filesystem::path &workingDirectory)
{
    const std::filesystem::path requested(program);
    auto executable_candidate = [&](const std::filesystem::path &candidate) {
        std::error_code error;
        const auto absolute = std::filesystem::absolute(candidate, error);
        if (error || ::access(absolute.c_str(), X_OK) != 0)
            return std::filesystem::path{};
        return absolute;
    };
    if (program.find('/') != std::string::npos)
        return executable_candidate(requested.is_absolute() ? requested : workingDirectory / requested);

    const char *pathEnvironment = std::getenv("PATH");
    const std::string pathList = pathEnvironment == nullptr ? "/bin:/usr/bin" : pathEnvironment;
    std::size_t begin = 0;
    while (begin <= pathList.size())
    {
        const std::size_t end = pathList.find(':', begin);
        const std::string_view entry(pathList.data() + begin,
                                     (end == std::string::npos ? pathList.size() : end) - begin);
        const auto directory = entry.empty() ? workingDirectory :
                               std::filesystem::path(entry).is_absolute() ? std::filesystem::path(entry) :
                                                                           workingDirectory / entry;
        auto candidate = executable_candidate(directory / requested);
        if (!candidate.empty())
            return candidate;
        if (end == std::string::npos)
            break;
        begin = end + 1;
    }
    return {};
}

int spawn_process_with_fork(pid_t &pid, ProcessRequest &request,
                            PipePair &outPipe, PipePair &errPipe)
{
    const auto executable = find_executable(request.argv.front(), request.workingDirectory);
    if (executable.empty())
        return ENOENT;

    auto arguments = make_argv(request.argv);
    char **childArguments = arguments.data();
    char **childEnvironment = environ;
    const char *workingDirectory = request.workingDirectory.c_str();
    const char *executablePath = executable.c_str();
    const int errorWriteFd = request.outputMode == ProcessOutputMode::Merged
                                 ? outPipe.writeFd
                                 : errPipe.writeFd;
    const int childFds[] = {outPipe.readFd, outPipe.writeFd, errPipe.readFd, errPipe.writeFd};
    pid = ::fork();
    if (pid < 0)
        return errno;
    if (pid == 0)
    {
        if (::setpgid(0, 0) != 0 || ::chdir(workingDirectory) != 0 ||
            ::dup2(outPipe.writeFd, STDOUT_FILENO) < 0)
            _exit(126);
        if (::dup2(errorWriteFd, STDERR_FILENO) < 0)
            _exit(126);
        if (request.sessionChannels != nullptr)
        {
            const auto &endpoints = request.sessionChannels->childEndpoints;
            if (endpoints.size() != 3)
                _exit(126);
            constexpr int childTargets[]{3, 4, 5};
            for (std::size_t index = 0; index < endpoints.size(); ++index)
            {
                if (endpoints[index].nativeHandle >
                    static_cast<std::uintptr_t>(std::numeric_limits<int>::max()))
                    _exit(126);
                const int source = static_cast<int>(endpoints[index].nativeHandle);
                if (source < 6 || ::dup2(source, childTargets[index]) < 0)
                    _exit(126);
            }
            for (const auto &endpoint : endpoints)
                ::close(static_cast<int>(endpoint.nativeHandle));
        }
        for (std::size_t i = 0; i < sizeof(childFds) / sizeof(childFds[0]); ++i)
        {
            const int fd = childFds[i];
            if (fd > STDERR_FILENO)
                ::close(fd);
        }
        ::execve(executablePath, childArguments, childEnvironment);
        _exit(127);
    }
    if (::setpgid(pid, pid) != 0 && errno != EACCES && errno != ESRCH)
    {
        const int error = errno;
        ::kill(pid, SIGKILL);
        int status = 0;
        while (::waitpid(pid, &status, 0) < 0 && errno == EINTR)
        {
        }
        return error;
    }
    return 0;
}

int spawn_process(pid_t &pid, ProcessRequest &request, PipePair &outPipe, PipePair &errPipe)
{
    if (forceForkFallbackForTesting.load(std::memory_order_acquire))
        return spawn_process_with_fork(pid, request, outPipe, errPipe);
#if defined(PLATFORMATOR_HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR) || \
    defined(PLATFORMATOR_HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCHDIR_NP)
    return spawn_process_with_posix_spawn(pid, request, outPipe, errPipe);
#else
    return spawn_process_with_fork(pid, request, outPipe, errPipe);
#endif
}

void emit_output(PosixProcessState &state, ProcessStream stream, const char *data, std::size_t size)
{
    if (!state.output || size == 0)
        return;
    ProcessOutput event;
    event.generation = state.generation;
    event.stream = stream;
    event.bytes.assign(data, size);
    state.output(std::move(event));
}

void drain_fd(PosixProcessState &state, int &fd, ProcessStream stream)
{
    if (fd < 0)
        return;
    std::array<char, kReadChunkBytes> buffer{};
    std::size_t remaining = kMaxReadBytesPerPoll;
    while (remaining > 0)
    {
        const std::size_t requested = std::min(remaining, buffer.size());
        const ssize_t count = ::read(fd, buffer.data(), requested);
        if (count > 0)
        {
            emit_output(state, stream, buffer.data(), static_cast<std::size_t>(count));
            remaining -= static_cast<std::size_t>(count);
            continue;
        }
        if (count == 0)
        {
            close_fd(fd);
            return;
        }
        if (errno == EINTR)
            continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return;
        if (!state.ioError.empty())
            state.ioError += "; ";
        state.ioError += std::string("pipe read failed: ") + std::strerror(errno);
        close_fd(fd);
        return;
    }
}

void finish(PosixProcessState &state)
{
    ProcessCompletion result;
    result.generation = state.generation;
    result.launched = true;
    if (WIFEXITED(state.waitStatus))
        result.exitCode = WEXITSTATUS(state.waitStatus);
    else if (WIFSIGNALED(state.waitStatus))
        result.terminatingSignal = WTERMSIG(state.waitStatus);
    result.error = std::move(state.ioError);

    auto callback = std::move(state.completion);
    close_fd(state.standardOutputFd);
    close_fd(state.standardErrorFd);
    state.pid = -1;
    state.processGroup = -1;
    state.generation = 0;
    state.childReaped = false;
    state.waitStatus = 0;
    state.stopRequested = false;
    state.killSent = false;
    state.ioError.clear();
    if (callback)
        callback(std::move(result));
}
} // namespace

bool process_start_posix(PosixProcessState &state, ProcessRequest request, std::uint64_t generation,
                         ProcessService::OutputCallback output,
                         ProcessService::CompletionCallback completion)
{
    if (state.pid > 0)
    {
        report_spawn_failure(generation, completion, EBUSY);
        return false;
    }
    if (request.argv.empty() || request.argv.front().empty())
    {
        report_spawn_failure(generation, completion, EINVAL);
        return false;
    }

    std::error_code filesystemError;
    if (request.workingDirectory.empty())
        request.workingDirectory = std::filesystem::current_path(filesystemError);
    if (filesystemError)
    {
        report_spawn_failure(generation, completion, filesystemError.value());
        return false;
    }

    if (request.sessionChannels != nullptr && request.sessionChannels->childEndpoints.size() != 3)
    {
        if (completion)
        {
            ProcessCompletion result;
            result.generation = generation;
            result.error = "session process launch requires exactly three inherited endpoints";
            completion(std::move(result));
        }
        return false;
    }
    request.workingDirectory = std::filesystem::absolute(request.workingDirectory, filesystemError);
    if (filesystemError || !std::filesystem::is_directory(request.workingDirectory, filesystemError) ||
        filesystemError)
    {
        report_spawn_failure(generation, completion, filesystemError ? filesystemError.value() : ENOTDIR);
        return false;
    }

    PipePair outputPipe;
    PipePair errorPipe;
    int error = create_pipe(outputPipe);
    if (error == 0 && request.outputMode == ProcessOutputMode::Separate)
        error = create_pipe(errorPipe);
    if (error == 0 && request.sessionChannels != nullptr)
    {
        error = reserve_session_spawn_descriptors(outputPipe);
        if (error == 0 && request.outputMode == ProcessOutputMode::Separate)
            error = reserve_session_spawn_descriptors(errorPipe);
    }
    if (error != 0)
    {
        close_pipe(outputPipe);
        close_pipe(errorPipe);
        report_spawn_failure(generation, completion, error);
        return false;
    }

    pid_t child = -1;
    error = spawn_process(child, request, outputPipe, errorPipe);
    close_fd(outputPipe.writeFd);
    close_fd(errorPipe.writeFd);
    if (error != 0)
    {
        close_pipe(outputPipe);
        close_pipe(errorPipe);
        report_spawn_failure(generation, completion, error);
        return false;
    }

    if (request.sessionChannels != nullptr)
        request.sessionChannels->close_child_endpoints();

    state.pid = child;
    state.processGroup = child;
    state.standardOutputFd = outputPipe.readFd;
    state.standardErrorFd = request.outputMode == ProcessOutputMode::Separate ? errorPipe.readFd : -1;
    state.outputMode = request.outputMode;
    state.generation = generation;
    state.output = std::move(output);
    state.completion = std::move(completion);
    state.childReaped = false;
    state.waitStatus = 0;
    state.stopRequested = false;
    state.killSent = false;
    state.ioError.clear();
    return true;
}

void process_poll_posix(PosixProcessState &state)
{
    if (state.pid <= 0)
        return;

    if (state.stopRequested && !state.killSent && std::chrono::steady_clock::now() >= state.killDeadline)
    {
        if (state.processGroup > 0)
            ::kill(-state.processGroup, SIGKILL);
        state.killSent = true;
    }

    drain_fd(state, state.standardOutputFd,
             state.outputMode == ProcessOutputMode::Merged ? ProcessStream::Merged : ProcessStream::Stdout);
    drain_fd(state, state.standardErrorFd, ProcessStream::Stderr);

    if (!state.childReaped)
    {
        const pid_t result = ::waitpid(state.pid, &state.waitStatus, WNOHANG);
        if (result == state.pid)
            state.childReaped = true;
        else if (result < 0 && errno == ECHILD)
        {
            state.childReaped = true;
            state.waitStatus = 0;
            if (!state.ioError.empty())
                state.ioError += "; ";
            state.ioError += "owned process was reaped outside ProcessService";
        }
    }

    drain_fd(state, state.standardOutputFd,
             state.outputMode == ProcessOutputMode::Merged ? ProcessStream::Merged : ProcessStream::Stdout);
    drain_fd(state, state.standardErrorFd, ProcessStream::Stderr);

    if (state.childReaped && state.standardOutputFd < 0 && state.standardErrorFd < 0)
        finish(state);
}

void process_stop_posix(PosixProcessState &state)
{
    if (state.pid <= 0 || state.stopRequested)
        return;
    state.stopRequested = true;
    state.killDeadline = std::chrono::steady_clock::now() + kStopGrace;
    if (state.processGroup > 0 && ::kill(-state.processGroup, SIGTERM) != 0 && errno != ESRCH)
    {
        if (!state.ioError.empty())
            state.ioError += "; ";
        state.ioError += std::string("could not send SIGTERM to owned process group: ") + std::strerror(errno);
    }
}

void process_force_cleanup_posix(PosixProcessState &state)
{
    if (state.processGroup > 0)
        ::kill(-state.processGroup, SIGKILL);
    if (state.pid > 0 && !state.childReaped)
    {
        int status = 0;
        pid_t result;
        do
        {
            result = ::waitpid(state.pid, &status, 0);
        } while (result < 0 && errno == EINTR);
        if (result == state.pid)
        {
            state.childReaped = true;
            state.waitStatus = status;
        }
    }
    close_fd(state.standardOutputFd);
    close_fd(state.standardErrorFd);
    state.pid = -1;
    state.processGroup = -1;
    state.generation = 0;
    state.childReaped = false;
    state.waitStatus = 0;
}

namespace
{
class PosixNativeProcess final : public NativeProcess
{
  public:
    bool start(ProcessRequest request, std::uint64_t generation,
               ProcessService::OutputCallback output,
               ProcessService::CompletionCallback completion) override
    {
        return process_start_posix(state_, std::move(request), generation,
                                   std::move(output), std::move(completion));
    }

    void poll() override { process_poll_posix(state_); }
    void stop() override { process_stop_posix(state_); }

    void shutdown() noexcept override
    {
        state_.output = {};
        state_.completion = {};
        process_stop_posix(state_);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
        while (state_.pid > 0 && std::chrono::steady_clock::now() < deadline)
        {
            process_poll_posix(state_);
            if (state_.pid > 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (state_.pid > 0)
            process_force_cleanup_posix(state_);
    }

    bool running() const noexcept override { return state_.pid > 0; }
    int process_id() const noexcept override
    {
        return state_.pid > 0 ? static_cast<int>(state_.pid) : -1;
    }

  private:
    PosixProcessState state_;
};
} // namespace

std::unique_ptr<NativeProcess> make_native_process()
{
    return std::make_unique<PosixNativeProcess>();
}

std::string_view native_process_containment_note() noexcept
{
    return "POSIX process groups stop foreground descendants; daemonized processes that call setsid "
           "escape ownership and require explicit target lifecycle registration.";
}

void set_native_process_test_hooks(NativeProcessTestHooks hooks)
{
    forceForkFallbackForTesting.store(hooks.forcePosixForkFallback, std::memory_order_release);
}

void clear_native_process_test_hooks()
{
    forceForkFallbackForTesting.store(false, std::memory_order_release);
}
} // namespace platformator_editor
