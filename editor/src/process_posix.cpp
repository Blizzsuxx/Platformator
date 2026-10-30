#include "process_internal.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <initializer_list>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>

#include <spawn.h>

extern char **environ;

namespace platformator_editor
{
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
int spawn_process(pid_t &pid, ProcessRequest &request, PipePair &outPipe, PipePair &errPipe)
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
#else
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

int spawn_process(pid_t &pid, ProcessRequest &request, PipePair &outPipe, PipePair &errPipe)
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
#endif

void emit_output(ProcessState &state, ProcessStream stream, const char *data, std::size_t size)
{
    if (!state.output || size == 0)
        return;
    ProcessOutput event;
    event.generation = state.generation;
    event.stream = stream;
    event.bytes.assign(data, size);
    state.output(std::move(event));
}

void drain_fd(ProcessState &state, int &fd, ProcessStream stream)
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

void finish(ProcessState &state)
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

bool process_start_posix(ProcessState &state, ProcessRequest request, std::uint64_t generation,
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

void process_poll_posix(ProcessState &state)
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

void process_stop_posix(ProcessState &state)
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

void process_force_cleanup_posix(ProcessState &state)
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
} // namespace platformator_editor
