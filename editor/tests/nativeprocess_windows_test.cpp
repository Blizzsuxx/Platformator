#include "platform/nativeprocess_testhooks.h"
#include "platform/sessionchannel_windows.h"
#include "process.h"
#include "test_support.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifndef PLATFORMATOR_EDITOR_PROCESS_FIXTURE
#error "PLATFORMATOR_EDITOR_PROCESS_FIXTURE must name the test fixture executable"
#endif

using namespace platformator_editor;
using namespace platformator_editor_test;
using namespace std::chrono_literals;

namespace
{
struct Capture
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

std::string path_to_utf8(const std::filesystem::path &path)
{
    const auto encoded = path.u8string();
    return std::string(reinterpret_cast<const char *>(encoded.data()), encoded.size());
}

ProcessRequest fixture_request(std::vector<std::string> arguments,
                               const std::filesystem::path &workingDirectory)
{
    ProcessRequest request;
    request.argv = {PLATFORMATOR_EDITOR_PROCESS_FIXTURE};
    request.argv.insert(request.argv.end(), std::make_move_iterator(arguments.begin()),
                        std::make_move_iterator(arguments.end()));
    request.workingDirectory = workingDirectory;
    return request;
}

void pump_until(ProcessService &process, Capture &capture, std::chrono::milliseconds timeout = 5s)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!capture.completed && std::chrono::steady_clock::now() < deadline)
    {
        process.poll();
        if (!capture.completed)
            std::this_thread::sleep_for(1ms);
    }
    require(capture.completed, "Windows process did not complete before the test deadline");
}

void wait_for_output(ProcessService &process, Capture &capture, std::string_view marker)
{
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (capture.output.find(marker) == std::string::npos &&
           std::chrono::steady_clock::now() < deadline)
    {
        process.poll();
        std::this_thread::sleep_for(1ms);
    }
    require(capture.output.find(marker) != std::string::npos,
            "Windows process fixture did not publish its ready marker");
}

DWORD parse_process_id(std::string_view output, std::string_view marker)
{
    const auto start = output.find(marker);
    require(start != std::string_view::npos, "process output is missing its descendant identity");
    const auto first = start + marker.size();
    const auto last = output.find_first_not_of("0123456789", first);
    return static_cast<DWORD>(std::stoul(std::string(output.substr(first, last - first))));
}

bool process_has_exited(DWORD processId)
{
    HANDLE process = ::OpenProcess(SYNCHRONIZE, FALSE, processId);
    if (process == nullptr)
        return ::GetLastError() == ERROR_INVALID_PARAMETER;
    const DWORD wait = ::WaitForSingleObject(process, 0);
    ::CloseHandle(process);
    return wait == WAIT_OBJECT_0;
}

void wait_for_process_exit(DWORD processId)
{
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!process_has_exited(processId) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(2ms);
    require(process_has_exited(processId), "owned Windows descendant survived Job Object termination");
}

void test_wide_argv_and_working_directory(const std::filesystem::path &root)
{
    const auto directory = root / std::filesystem::u8path("wide 雪 path");
    std::filesystem::create_directories(directory);
    ProcessService process;
    Capture capture;
    require(process.start(fixture_request({"args", "two words", "雪"}, directory), 1201,
                          capture.on_output(), capture.on_completion()),
            "wide argv fixture should launch");
    pump_until(process, capture);
    require(capture.result.launched && capture.result.exitCode == 0 &&
                capture.output.find("arg:two words\n") != std::string::npos &&
                capture.output.find("arg:雪\n") != std::string::npos &&
                capture.output.find("cwd:" + path_to_utf8(directory)) != std::string::npos,
            "Windows argv quoting, UTF-8 conversion, and working directory should round-trip");
}

void test_restricted_handles_and_setup_failures(const std::filesystem::path &root)
{
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE sentinel = ::CreateEventW(&security, TRUE, FALSE, nullptr);
    require(sentinel != nullptr, "inheritable sentinel event should be created");
    const auto sentinelValue = std::to_string(reinterpret_cast<std::uintptr_t>(sentinel));
    ProcessService inheritedCheck;
    Capture inheritedCapture;
    require(inheritedCheck.start(fixture_request({"--check-handle", sentinelValue}, root), 1202,
                                 inheritedCapture.on_output(), inheritedCapture.on_completion()),
            "handle whitelist fixture should launch");
    pump_until(inheritedCheck, inheritedCapture);
    ::CloseHandle(sentinel);
    require(inheritedCapture.result.launched && inheritedCapture.result.exitCode == 0,
            "an inheritable parent sentinel must not leak through the explicit handle whitelist");

    const auto marker = root / L"must-not-run.txt";
    const auto markerUtf8 = path_to_utf8(marker);
    NativeProcessTestHooks hooks;
    bool whitelistFailureObserved = false;
    hooks.failHandleWhitelistUpdate = true;
    hooks.onHandleWhitelistFailure = [&] { whitelistFailureObserved = true; };
    set_native_process_test_hooks(std::move(hooks));
    ProcessService whitelistFailure;
    Capture whitelistCapture;
    require(!whitelistFailure.start(fixture_request({"--write-marker", markerUtf8}, root), 1203,
                                    whitelistCapture.on_output(), whitelistCapture.on_completion()),
            "a failed inherited-handle restriction must reject the launch");
    clear_native_process_test_hooks();
    require(whitelistFailureObserved && whitelistCapture.completed && !whitelistCapture.result.launched &&
                !std::filesystem::exists(marker),
            "failed handle inheritance must not create or run a child");

    bool childReaped = false;
    hooks = {};
    hooks.failJobAssignment = true;
    hooks.onSuspendedChildCleanup = [&](bool reaped) { childReaped = reaped; };
    set_native_process_test_hooks(std::move(hooks));
    ProcessService assignmentFailure;
    Capture assignmentCapture;
    require(!assignmentFailure.start(fixture_request({"--write-marker", markerUtf8}, root), 1204,
                                     assignmentCapture.on_output(), assignmentCapture.on_completion()),
            "a failed Job assignment must reject the launch");
    clear_native_process_test_hooks();
    require(childReaped && assignmentCapture.completed && !assignmentCapture.result.launched &&
                !std::filesystem::exists(marker),
            "failed Job assignment must terminate and reap the child without resuming it");
}

void test_job_owns_descendants_and_stop_is_bounded(const std::filesystem::path &root)
{
    ProcessService owned;
    ProcessService unrelated;
    Capture ownedCapture;
    Capture unrelatedCapture;
    require(owned.start(fixture_request({"grandchild"}, root), 1205,
                        ownedCapture.on_output(), ownedCapture.on_completion()),
            "owned process tree should launch");
    require(unrelated.start(fixture_request({"hang"}, root), 1206,
                            unrelatedCapture.on_output(), unrelatedCapture.on_completion()),
            "unrelated process should launch");
    wait_for_output(owned, ownedCapture, "grandchild:");
    const DWORD descendant = parse_process_id(ownedCapture.output, "grandchild:");

    const auto start = std::chrono::steady_clock::now();
    owned.stop();
    pump_until(owned, ownedCapture, 2s);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    require(elapsed < 2s && ownedCapture.result.launched,
            "Job Object stop should terminate within the two-second force-stop bound");
    wait_for_process_exit(descendant);
    require(unrelated.running(), "stopping one process job must preserve an unrelated process");
    unrelated.stop();
    pump_until(unrelated, unrelatedCapture, 2s);
}

void test_inherited_session_channels(const std::filesystem::path &root)
{
    auto provider = make_windows_session_channel_provider();
    std::string error;
    require(provider != nullptr, "Windows session channel provider should be available");
    require(!provider->create_pair(session_frame_mapping_limit + 1, error) && !error.empty(),
            "session mapping requests above the bounded limit should fail cleanly");
    error.clear();
    auto channels = provider->create_pair(session_frame_mapping_limit, error);
    require(channels != nullptr && channels->childEndpoints.size() == 3 &&
                channels->frameMapping != nullptr &&
                channels->frameMapping->size() == session_frame_mapping_limit,
            "session channels should create two private pipes and a bounded shared mapping");

    ProcessRequest request = fixture_request({"session"}, root);
    request.argv.emplace_back("--editor-session-v1");
    for (const auto &endpoint : channels->childEndpoints)
    {
        request.argv.push_back(endpoint.argument);
        request.argv.push_back(endpoint.opaqueValue);
    }
    request.sessionChannels = channels.get();
    ProcessService process;
    Capture capture;
    require(process.start(std::move(request), 1207, capture.on_output(), capture.on_completion()),
            "session fixture should launch with only its explicit inherited endpoints");

    const std::byte command{static_cast<unsigned char>('C')};
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    bool commandSent = false;
    while (!commandSent && std::chrono::steady_clock::now() < deadline)
    {
        const auto write = channels->commands->write(std::span<const std::byte>(&command, 1));
        if (write.status == SessionChannelStatus::Progress)
            commandSent = write.bytesTransferred == 1;
        else if (write.status != SessionChannelStatus::WouldBlock)
            throw std::runtime_error("parent session command pipe failed: " + write.error);
        process.poll();
        std::this_thread::sleep_for(1ms);
    }
    require(commandSent, "parent session command should reach the child endpoint");

    char eventByte = 0;
    bool eventReceived = false;
    while (!eventReceived && std::chrono::steady_clock::now() < deadline)
    {
        std::byte event{};
        const auto read = channels->events->read(std::span<std::byte>(&event, 1));
        if (read.status == SessionChannelStatus::Progress)
        {
            require(read.bytesTransferred == 1, "child session event should be one byte");
            eventByte = static_cast<char>(event);
            eventReceived = true;
        }
        else if (read.status != SessionChannelStatus::WouldBlock)
            throw std::runtime_error("parent session event pipe closed before its event: " + read.error);
        process.poll();
        std::this_thread::sleep_for(1ms);
    }
    require(eventReceived && eventByte == 'E', "child event should reach the parent endpoint");
    pump_until(process, capture);
    require(capture.result.launched && capture.result.exitCode == 0,
            "session fixture should map shared memory and close normally");
    require(channels->frameMapping->bytes().front() == std::byte{0x5a},
            "child writes should be visible in the parent shared mapping");

    bool observedEof = false;
    const auto eofDeadline = std::chrono::steady_clock::now() + 3s;
    while (!observedEof && std::chrono::steady_clock::now() < eofDeadline)
    {
        std::byte event{};
        const auto read = channels->events->read(std::span<std::byte>(&event, 1));
        observedEof = read.status == SessionChannelStatus::Closed;
        require(observedEof || read.status == SessionChannelStatus::WouldBlock,
                "event channel should drain to EOF after the child exits");
        std::this_thread::sleep_for(1ms);
    }
    require(observedEof, "child endpoint closure should reach the parent as EOF");

    channels->close_child_endpoints();
    channels->close_child_endpoints();
    channels->commands->cancel();
    channels->commands->cancel();
    channels->events->close();
    channels->events->close();
    channels->frameMapping->close();
    channels->frameMapping->close();
    std::byte probe{};
    require(channels->commands->write(std::span<const std::byte>(&command, 1)).status ==
                SessionChannelStatus::Cancelled &&
                channels->events->read(std::span<std::byte>(&probe, 1)).status ==
                SessionChannelStatus::Closed && channels->frameMapping->size() == 0,
            "repeated channel cancellation, close and unmap should be idempotent");
}
} // namespace

int main()
{
    try
    {
        TemporaryDirectory temporary;
        test_wide_argv_and_working_directory(temporary.path());
        test_restricted_handles_and_setup_failures(temporary.path());
        test_job_owns_descendants_and_stop_is_bounded(temporary.path());
        test_inherited_session_channels(temporary.path());
        return 0;
    }
    catch (const std::exception &error)
    {
        clear_native_process_test_hooks();
        std::cerr << "platformator_editor_nativeprocess_windows_test: " << error.what() << '\n';
        return 1;
    }
}
