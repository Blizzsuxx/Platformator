#include "platform/nativeprocess.h"
#include "platform/nativeprocess_testhooks.h"
#include "platform/sessionchannel_posix.h"
#include "process.h"
#include "test_support.h"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <array>
#include <cstddef>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <thread>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef PLATFORMATOR_EDITOR_PROCESS_FIXTURE
#error "PLATFORMATOR_EDITOR_PROCESS_FIXTURE must name the test fixture executable"
#endif

using namespace platformator_editor;
using namespace platformator_editor_test;
using namespace std::chrono_literals;

extern char **environ;

namespace
{
struct ProcessCapture
{
    std::string output;
    bool completed = false;
    ProcessCompletion result;

    ProcessService::OutputCallback on_output()
    {
        return [this](ProcessOutput event) { output += event.bytes; };
    }
    ProcessService::CompletionCallback on_completion()
    {
        return [this](ProcessCompletion event) {
            result = std::move(event);
            completed = true;
        };
    }
};

class ScopedClosedStandardStreams
{
  public:
    ScopedClosedStandardStreams()
    {
        for (std::size_t index = 0; index < saved_.size(); ++index)
        {
            saved_[index] = ::fcntl(static_cast<int>(index), F_DUPFD_CLOEXEC, 64);
            if (saved_[index] < 0)
            {
                error_ = "could not preserve a standard stream for the descriptor-collision case";
                return;
            }
        }
        for (std::size_t index = 0; index < saved_.size(); ++index)
        {
            closed_[index] = true;
            ::close(static_cast<int>(index));
        }
        ready_ = true;
    }

    ~ScopedClosedStandardStreams()
    {
        for (std::size_t index = 0; index < saved_.size(); ++index)
        {
            if (saved_[index] < 0)
                continue;
            if (closed_[index])
                ::dup2(saved_[index], static_cast<int>(index));
            ::close(saved_[index]);
        }
    }

    ScopedClosedStandardStreams(const ScopedClosedStandardStreams &) = delete;
    ScopedClosedStandardStreams &operator=(const ScopedClosedStandardStreams &) = delete;

    bool ready() const noexcept { return ready_; }
    const std::string &error() const noexcept { return error_; }

  private:
    std::array<int, 3> saved_{-1, -1, -1};
    std::array<bool, 3> closed_{};
    bool ready_ = false;
    std::string error_;
};

class ScopedForkFallback
{
  public:
    ScopedForkFallback()
    {
        NativeProcessTestHooks hooks;
        hooks.forcePosixForkFallback = true;
        set_native_process_test_hooks(std::move(hooks));
    }
    ~ScopedForkFallback() { clear_native_process_test_hooks(); }
    ScopedForkFallback(const ScopedForkFallback &) = delete;
    ScopedForkFallback &operator=(const ScopedForkFallback &) = delete;
};

ProcessRequest fixture_request(std::string mode, const std::filesystem::path &workingDirectory)
{
    ProcessRequest request;
    request.argv = {PLATFORMATOR_EDITOR_PROCESS_FIXTURE, std::move(mode)};
    request.workingDirectory = workingDirectory;
    return request;
}

void wait_for_output(ProcessService &process, ProcessCapture &capture,
                     std::string_view marker, std::chrono::milliseconds timeout = 2s)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (capture.output.find(marker) == std::string::npos &&
           std::chrono::steady_clock::now() < deadline)
    {
        process.poll();
        std::this_thread::sleep_for(1ms);
    }
    require(capture.output.find(marker) != std::string::npos,
            "native process fixture did not publish its ready marker");
}

void pump_until(ProcessService &process, ProcessCapture &capture,
                std::chrono::milliseconds timeout = 5s)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!capture.completed && std::chrono::steady_clock::now() < deadline)
    {
        process.poll();
        std::this_thread::sleep_for(1ms);
    }
    require(capture.completed, "native process did not complete before the deadline");
}

pid_t parse_field(const std::string &output, std::string_view field)
{
    const auto start = output.find(field);
    require(start != std::string::npos, "fixture output is missing process identity field");
    const auto valueStart = start + field.size();
    const auto end = output.find_first_of(" \n", valueStart);
    return static_cast<pid_t>(std::stol(output.substr(valueStart, end - valueStart)));
}

bool process_is_live_or_non_zombie(pid_t pid)
{
    if (pid <= 0)
        return false;
    if (::kill(pid, 0) != 0 && errno == ESRCH)
        return false;
#if defined(__linux__)
    std::ifstream stat("/proc/" + std::to_string(pid) + "/stat");
    std::string line;
    if (!std::getline(stat, line))
        return false;
    const auto close = line.rfind(')');
    if (close == std::string::npos || close + 2 >= line.size())
        return true;
    return line[close + 2] != 'Z';
#else
    return true;
#endif
}

void wait_for_exit(pid_t pid)
{
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (process_is_live_or_non_zombie(pid) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(2ms);
    require(!process_is_live_or_non_zombie(pid), "owned process fixture survived cleanup");
}

class ScopedExternalProcess
{
  public:
    explicit ScopedExternalProcess(pid_t pid) : pid_(pid) {}
    ~ScopedExternalProcess()
    {
        if (pid_ > 0)
            ::kill(pid_, SIGKILL);
    }
    ScopedExternalProcess(const ScopedExternalProcess &) = delete;
    ScopedExternalProcess &operator=(const ScopedExternalProcess &) = delete;

  private:
    pid_t pid_;
};

void test_foreground_grandchild_is_owned_and_reaped(const std::filesystem::path &directory)
{
    ProcessService process;
    ProcessCapture capture;
    require(process.start(fixture_request("grandchild", directory), 901,
                          capture.on_output(), capture.on_completion()),
            "foreground grandchild fixture should launch");
    wait_for_output(process, capture, "grandchild:");
    const pid_t parent = static_cast<pid_t>(process.process_id());
    const pid_t grandchild = parse_field(capture.output, "grandchild:");
    require(parent > 0 && grandchild > 0 && ::getpgid(parent) == parent &&
                ::getpgid(grandchild) == parent,
            "foreground child and grandchild must share the service-owned process group");
    process.stop();
    pump_until(process, capture, 4s);
    require(capture.result.generation == 901, "completion retains the native operation generation");
    wait_for_exit(grandchild);
}

void test_unrelated_same_executable_process_survives_stop(const std::filesystem::path &directory)
{
    ProcessService owned;
    ProcessService unrelated;
    ProcessCapture ownedCapture;
    ProcessCapture unrelatedCapture;
    require(owned.start(fixture_request("hang", directory), 902,
                        ownedCapture.on_output(), ownedCapture.on_completion()),
            "owned same-name process should launch");
    require(unrelated.start(fixture_request("hang", directory), 903,
                            unrelatedCapture.on_output(), unrelatedCapture.on_completion()),
            "unrelated same-name process should launch");
    wait_for_output(owned, ownedCapture, "pid:");
    wait_for_output(unrelated, unrelatedCapture, "pid:");
    const pid_t ownedPid = static_cast<pid_t>(owned.process_id());
    const pid_t unrelatedPid = static_cast<pid_t>(unrelated.process_id());
    require(ownedPid != unrelatedPid && ::getpgid(ownedPid) != ::getpgid(unrelatedPid),
            "same-executable fixtures must have separate ownership groups");

    owned.stop();
    pump_until(owned, ownedCapture, 4s);
    require(process_is_live_or_non_zombie(unrelatedPid) && unrelated.running() &&
                ::getpgid(unrelatedPid) == unrelatedPid,
            "stopping one owned group must preserve an unrelated same-executable process");
    unrelated.stop();
    pump_until(unrelated, unrelatedCapture, 4s);
}

void test_daemon_escape_is_reported_and_fixture_is_cleaned(const std::filesystem::path &directory)
{
    const auto note = native_process_containment_note();
    require(note.find("setsid") != std::string_view::npos &&
                note.find("lifecycle registration") != std::string_view::npos,
            "POSIX ownership disclosure must explain daemon escape and required target registration");
    ProcessService process;
    ProcessCapture capture;
    require(process.start(fixture_request("daemon-escape", directory), 904,
                          capture.on_output(), capture.on_completion()),
            "daemon-escape fixture should launch");
    wait_for_output(process, capture, "grandchild:");
    const pid_t parent = static_cast<pid_t>(process.process_id());
    const pid_t daemon = parse_field(capture.output, "grandchild:");
    ScopedExternalProcess cleanup(daemon);
    require(daemon > 0, "daemon fixture must publish a process identity");
    const auto sessionDeadline = std::chrono::steady_clock::now() + 2s;
    while (::getpgid(daemon) != daemon && std::chrono::steady_clock::now() < sessionDeadline)
        std::this_thread::sleep_for(1ms);
    require(::getpgid(daemon) == daemon,
            "daemon fixture must detach into its own session and process group");
    process.stop();
    pump_until(process, capture, 4s);
    require(!process_is_live_or_non_zombie(parent) && process_is_live_or_non_zombie(daemon),
            "stopping the foreground group must leave the deliberately detached fixture for explicit cleanup");
    require(note.find("foreground descendants") != std::string_view::npos,
            "reported limitation must distinguish foreground ownership from daemon containment");
    ::kill(daemon, SIGKILL);
    wait_for_exit(daemon);
}

int session_exec_child(const char *executable, bool emitLogs = false)
{
    if (emitLogs)
    {
        constexpr char out[] = "session-stdout\n";
        constexpr char err[] = "session-stderr\n";
        if (::write(STDOUT_FILENO, out, sizeof(out) - 1) != static_cast<ssize_t>(sizeof(out) - 1) ||
            ::write(STDERR_FILENO, err, sizeof(err) - 1) != static_cast<ssize_t>(sizeof(err) - 1))
            return 40;
    }
    for (int descriptor : {3, 4, 5})
        if (::fcntl(descriptor, F_GETFD) < 0)
            return 41;

    std::array<char, 4> command{};
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    ssize_t received = -1;
    while (std::chrono::steady_clock::now() < deadline)
    {
        received = ::read(3, command.data(), command.size());
        if (received >= 0)
            break;
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            return 42;
        std::this_thread::sleep_for(1ms);
    }
    if (received != 3 || std::string_view(command.data(), 3) != "cmd")
        return 43;

    struct stat mappingStat{};
    if (::fstat(5, &mappingStat) != 0 || mappingStat.st_size != 128)
        return 44;
    void *mapping = ::mmap(nullptr, 128, PROT_READ | PROT_WRITE, MAP_SHARED, 5, 0);
    if (mapping == MAP_FAILED)
        return 45;
    auto *bytes = static_cast<unsigned char *>(mapping);
    if (std::memcmp(bytes, "parent", 6) != 0)
    {
        ::munmap(mapping, 128);
        return 46;
    }
    bytes[32] = 0x42;
    ::munmap(mapping, 128);

    const char event = 'e';
    ssize_t sent = -1;
    while (std::chrono::steady_clock::now() < deadline)
    {
        sent = ::write(4, &event, 1);
        if (sent >= 0)
            break;
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            return 47;
        std::this_thread::sleep_for(1ms);
    }
    if (sent != 1)
        return 48;

    std::string error;
    if (!mark_session_child_descriptors_close_on_exec(error))
        return 49;
    char *arguments[]{const_cast<char *>(executable), const_cast<char *>("--session-no-fds"), nullptr};
    ::execv(executable, arguments);
    return 50;
}

int session_no_fds_child()
{
    for (int descriptor : {3, 4, 5})
    {
        errno = 0;
        if (::fcntl(descriptor, F_GETFD) >= 0 || errno != EBADF)
            return 51;
    }
    return 0;
}

void test_inherited_session_channels(const std::filesystem::path &executable)
{
    auto provider = make_posix_session_channel_provider();
    std::string error;
    require(!provider->create_pair(0, error) && !error.empty(),
            "zero-sized session mapping must fail with a useful error");
    require(!provider->create_pair(session_frame_mapping_limit + 1, error) && !error.empty(),
            "oversized session mapping must fail with a useful error");

    constexpr std::size_t mappingSize = 128;
    auto pair = provider->create_pair(mappingSize, error);
    require(pair != nullptr && error.empty(), "valid POSIX session channel pair should be created");
    require(pair->childEndpoints.size() == 3 && pair->frameMapping &&
                pair->frameMapping->size() == mappingSize && pair->frameMapping->bytes().size() == mappingSize,
            "session pair owns two channels and a bounded shared mapping");
    std::array<int, 3> sources{};
    for (std::size_t index = 0; index < pair->childEndpoints.size(); ++index)
    {
        const auto &endpoint = pair->childEndpoints[index];
        sources[index] = static_cast<int>(endpoint.nativeHandle);
        require(endpoint.opaqueValue == std::to_string(index + 3) && sources[index] >= 6,
                "child endpoint descriptors retain their ordered reserved targets and safe sources");
        const int flags = ::fcntl(sources[index], F_GETFD);
        require(flags >= 0 && (flags & FD_CLOEXEC) != 0,
                "parent copies of child descriptors must be close-on-exec");
    }
    require(sources[0] != sources[1] && sources[0] != sources[2] && sources[1] != sources[2],
            "each inherited endpoint must own a distinct source descriptor");
    struct stat mappingStat{};
    require(::fstat(sources[2], &mappingStat) == 0 && mappingStat.st_nlink == 0,
            "shared memory must be unlinked as soon as its descriptor is created");
    auto mapping = pair->frameMapping->bytes();
    std::memcpy(mapping.data(), "parent", 6);

    posix_spawn_file_actions_t actions{};
    require(::posix_spawn_file_actions_init(&actions) == 0,
            "spawn file actions should initialize");
    const bool actionsAdded = add_posix_session_spawn_actions(*pair, actions, error);
    require(actionsAdded, "session endpoint descriptors should map to child descriptors 3/4/5");
    pid_t child = -1;
    char *arguments[]{const_cast<char *>(executable.c_str()), const_cast<char *>("--session-child"), nullptr};
    const int spawnError = ::posix_spawn(&child, executable.c_str(), &actions, nullptr, arguments, environ);
    ::posix_spawn_file_actions_destroy(&actions);
    require(spawnError == 0 && child > 0, "session child should spawn with inherited endpoints");

    pair->close_child_endpoints();
    pair->close_child_endpoints();
    for (const int source : sources)
        require(::fcntl(source, F_GETFD) < 0 && errno == EBADF,
                "closing child endpoints must release each parent source exactly once");

    const std::array command{std::byte{'c'}, std::byte{'m'}, std::byte{'d'}};
    const auto writeDeadline = std::chrono::steady_clock::now() + 2s;
    SessionChannelResult commandResult;
    do
    {
        commandResult = pair->commands->write(command);
        if (commandResult.status == SessionChannelStatus::WouldBlock)
            std::this_thread::sleep_for(1ms);
    } while (commandResult.status == SessionChannelStatus::WouldBlock &&
             std::chrono::steady_clock::now() < writeDeadline);
    require(commandResult.status == SessionChannelStatus::Progress && commandResult.bytesTransferred == 3,
            "parent command writes should reach the inherited child pipe");

    std::array<std::byte, 1> event{};
    const auto eventDeadline = std::chrono::steady_clock::now() + 2s;
    SessionChannelResult eventResult;
    do
    {
        eventResult = pair->events->read(event);
        if (eventResult.status == SessionChannelStatus::WouldBlock)
            std::this_thread::sleep_for(1ms);
    } while (eventResult.status == SessionChannelStatus::WouldBlock &&
             std::chrono::steady_clock::now() < eventDeadline);
    require(eventResult.status == SessionChannelStatus::Progress && eventResult.bytesTransferred == 1 &&
                event[0] == std::byte{'e'},
            "child event writes should reach the parent pipe");

    int childStatus = 0;
    require(::waitpid(child, &childStatus, 0) == child && WIFEXITED(childStatus) && WEXITSTATUS(childStatus) == 0,
            "session child must close descriptors before exec of the later game process");
    require(mapping[32] == std::byte{0x42},
            "child and parent should observe writes through the shared frame mapping");
    require(pair->events->read(event).status == SessionChannelStatus::Closed,
            "event channel reports EOF after the inherited child endpoint closes at exec");
    require(pair->commands->write(command).status == SessionChannelStatus::Closed,
            "broken command pipe reports closed without delivering SIGPIPE to the process");
    pair->commands->cancel();
    pair->commands->cancel();
    require(pair->commands->write(command).status == SessionChannelStatus::Cancelled,
            "cancel is idempotent and remains distinguishable from ordinary close");
    pair->events->close();
    pair->events->close();
    pair->frameMapping->close();
    pair->frameMapping->close();
    require(pair->events->read(event).status == SessionChannelStatus::Closed &&
                pair->frameMapping->size() == 0 && pair->frameMapping->bytes().empty(),
            "repeated shutdown closes channels and unmaps shared memory exactly once");
}

void test_session_channels_launch_through_process_service(const std::filesystem::path &executable)
{
    ScopedClosedStandardStreams closedStandardStreams;
    require(closedStandardStreams.ready(), closedStandardStreams.error());
    ScopedForkFallback forkFallback;
    auto provider = make_posix_session_channel_provider();
    std::string error;
    constexpr std::size_t mappingSize = 128;
    auto pair = provider->create_pair(mappingSize, error);
    require(pair != nullptr && error.empty(), "ProcessService session pair should be created");
    std::memcpy(pair->frameMapping->bytes().data(), "parent", 6);

    ProcessService process;
    ProcessRequest request;
    request.argv = {executable.string(), "--session-child-logs"};
    request.workingDirectory = executable.parent_path();
    request.outputMode = ProcessOutputMode::Separate;
    request.sessionChannels = pair.get();
    std::string stdoutLog;
    std::string stderrLog;
    bool completed = false;
    ProcessCompletion completion;
    require(process.start(std::move(request), 91,
                [&](ProcessOutput output) {
                    if (output.stream == ProcessStream::Stdout)
                        stdoutLog += output.bytes;
                    else if (output.stream == ProcessStream::Stderr)
                        stderrLog += output.bytes;
                },
                [&](ProcessCompletion result) {
                    completion = std::move(result);
                    completed = true;
                }),
            "ProcessService should spawn a child with its private session endpoints");
    require(std::all_of(pair->childEndpoints.begin(), pair->childEndpoints.end(),
                        [](const InheritedSessionEndpoint &endpoint) {
                            return endpoint.nativeHandle == 0;
                        }),
            "parent closes duplicate child endpoints immediately after spawn");

    const std::array command{std::byte{'c'}, std::byte{'m'}, std::byte{'d'}};
    const auto writeDeadline = std::chrono::steady_clock::now() + 2s;
    SessionChannelResult writeResult;
    do
    {
        writeResult = pair->commands->write(command);
        if (writeResult.status == SessionChannelStatus::WouldBlock)
            std::this_thread::sleep_for(1ms);
    } while (writeResult.status == SessionChannelStatus::WouldBlock &&
             std::chrono::steady_clock::now() < writeDeadline);
    require(writeResult.status == SessionChannelStatus::Progress && writeResult.bytesTransferred == 3,
            "parent command channel remains usable after spawn closes its duplicate child end");

    std::array<std::byte, 1> event{};
    bool gotEvent = false;
    const auto finishDeadline = std::chrono::steady_clock::now() + 3s;
    while (std::chrono::steady_clock::now() < finishDeadline && (!completed || !gotEvent))
    {
        if (!gotEvent)
        {
            const auto result = pair->events->read(event);
            if (result.status == SessionChannelStatus::Progress)
                gotEvent = result.bytesTransferred == 1 && event[0] == std::byte{'e'};
        }
        process.poll();
        if (!completed || !gotEvent)
            std::this_thread::sleep_for(1ms);
    }
    require(completed && completion.launched && completion.generation == 91 && completion.exitCode == 0,
            "owned session child must be reaped through ProcessService");
    require(gotEvent && pair->frameMapping->bytes()[32] == std::byte{0x42},
            "inherited event and shared mapping channels must reach the child");
    require(stdoutLog == "session-stdout\n" && stderrLog == "session-stderr\n",
            "session protocol remains separate from stdout and stderr logs");
}
} // namespace

int main(int argc, char **argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--session-child")
        return session_exec_child(argv[0]);
    if (argc == 2 && std::string_view(argv[1]) == "--session-child-logs")
        return session_exec_child(argv[0], true);
    if (argc == 2 && std::string_view(argv[1]) == "--session-no-fds")
        return session_no_fds_child();
    try
    {
        TemporaryDirectory temporary;
        test_foreground_grandchild_is_owned_and_reaped(temporary.path());
        test_unrelated_same_executable_process_survives_stop(temporary.path());
        test_daemon_escape_is_reported_and_fixture_is_cleaned(temporary.path());
        test_inherited_session_channels(std::filesystem::absolute(argv[0]));
        test_session_channels_launch_through_process_service(std::filesystem::absolute(argv[0]));
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "platformator_editor_nativeprocess_test: " << error.what() << '\n';
        return 1;
    }
}
