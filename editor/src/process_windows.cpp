#include "process_internal.h"
#include "platform/nativeprocess_testhooks.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cwctype>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace platformator_editor
{
namespace
{
constexpr std::size_t kReadChunkBytes = 8192;
constexpr std::size_t kMaxReadBytesPerPoll = 64 * 1024;
std::mutex testHooksMutex;
NativeProcessTestHooks testHooks;

NativeProcessTestHooks current_test_hooks()
{
    std::lock_guard lock(testHooksMutex);
    return testHooks;
}

class ScopedHandle
{
  public:
    explicit ScopedHandle(HANDLE handle = nullptr) noexcept : handle_(handle) {}
    ~ScopedHandle() { reset(); }
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

    HANDLE get() const noexcept { return handle_; }
    HANDLE release() noexcept
    {
        const HANDLE result = handle_;
        handle_ = nullptr;
        return result;
    }
    void reset(HANDLE handle = nullptr) noexcept
    {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE)
            ::CloseHandle(handle_);
        handle_ = handle;
    }

  private:
    HANDLE handle_ = nullptr;
};

struct WindowsProcessState
{
    HANDLE process = nullptr;
    HANDLE job = nullptr;
    HANDLE standardOutput = nullptr;
    HANDLE standardError = nullptr;
    DWORD processId = 0;
    ProcessOutputMode outputMode = ProcessOutputMode::Merged;
    std::uint64_t generation = 0;
    ProcessService::OutputCallback output;
    ProcessService::CompletionCallback completion;
    bool exited = false;
    DWORD exitCode = STILL_ACTIVE;
    bool stopRequested = false;
    std::string ioError;
};

std::string utf8_from_wide(std::wstring_view value)
{
    if (value.empty())
        return {};
    const int bytes = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                            static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (bytes <= 0)
        return "Windows error message could not be converted to UTF-8";
    std::string result(static_cast<std::size_t>(bytes), '\0');
    if (::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                              static_cast<int>(value.size()), result.data(), bytes, nullptr, nullptr) != bytes)
        return "Windows error message could not be converted to UTF-8";
    return result;
}

bool wide_from_utf8(std::string_view value, std::wstring &result)
{
    result.clear();
    if (value.empty())
        return true;
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return false;
    const int characters = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                                  static_cast<int>(value.size()), nullptr, 0);
    if (characters <= 0)
        return false;
    result.resize(static_cast<std::size_t>(characters));
    return ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                 static_cast<int>(value.size()), result.data(), characters) == characters;
}

std::string windows_error(const char *operation, DWORD error)
{
    LPWSTR message = nullptr;
    const DWORD length = ::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                              FORMAT_MESSAGE_IGNORE_INSERTS,
                                          nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                          reinterpret_cast<LPWSTR>(&message), 0, nullptr);
    std::string result(operation);
    result += ": ";
    if (length != 0 && message != nullptr)
    {
        std::wstring_view text(message, length);
        while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n'))
            text.remove_suffix(1);
        result += utf8_from_wide(text);
        ::LocalFree(message);
    }
    else
        result += "Windows error " + std::to_string(error);
    return result;
}

void append_error(std::string &destination, std::string message)
{
    if (!destination.empty())
        destination += "; ";
    destination += std::move(message);
}

void report_start_failure(std::uint64_t generation,
                          ProcessService::CompletionCallback &completion,
                          std::string error)
{
    if (!completion)
        return;
    ProcessCompletion result;
    result.generation = generation;
    result.error = std::move(error);
    completion(std::move(result));
}

std::wstring quote_windows_argument(std::wstring_view argument)
{
    const bool needsQuotes = argument.empty() ||
        std::any_of(argument.begin(), argument.end(), [](wchar_t ch) {
            return std::iswspace(ch) != 0 || ch == L'"';
        });
    if (!needsQuotes)
        return std::wstring(argument);

    std::wstring result;
    result.push_back(L'"');
    std::size_t backslashes = 0;
    for (const wchar_t ch : argument)
    {
        if (ch == L'\\')
        {
            ++backslashes;
            continue;
        }
        if (ch == L'"')
        {
            result.append(backslashes * 2 + 1, L'\\');
            result.push_back(L'"');
            backslashes = 0;
            continue;
        }
        result.append(backslashes, L'\\');
        backslashes = 0;
        result.push_back(ch);
    }
    result.append(backslashes * 2, L'\\');
    result.push_back(L'"');
    return result;
}

bool make_command_line(const std::vector<std::string> &arguments,
                       std::wstring &commandLine, std::wstring &application,
                       const std::filesystem::path &workingDirectory,
                       std::string &error)
{
    if (arguments.empty() || arguments.front().empty())
    {
        error = "process argv must contain a nonempty executable path";
        return false;
    }
    std::vector<std::wstring> wideArguments;
    wideArguments.reserve(arguments.size());
    for (const auto &argument : arguments)
    {
        if (argument.find('\0') != std::string::npos)
        {
            error = "process argv contains an embedded NUL byte";
            return false;
        }
        std::wstring wide;
        if (!wide_from_utf8(argument, wide))
        {
            error = "process argv contains invalid UTF-8";
            return false;
        }
        wideArguments.push_back(std::move(wide));
    }

    application = wideArguments.front();
    std::filesystem::path applicationPath(application);
    if (applicationPath.has_parent_path() && applicationPath.is_relative())
    {
        std::error_code filesystemError;
        const auto absolute = std::filesystem::absolute(workingDirectory / applicationPath, filesystemError);
        if (filesystemError)
        {
            error = "could not resolve process executable path: " + filesystemError.message();
            return false;
        }
        application = absolute.native();
    }

    for (const auto &argument : wideArguments)
    {
        if (!commandLine.empty())
            commandLine.push_back(L' ');
        commandLine += quote_windows_argument(argument);
        if (commandLine.size() >= 32767)
        {
            error = "Windows process command line exceeds 32,766 UTF-16 code units";
            return false;
        }
    }
    return true;
}

struct PipePair
{
    ScopedHandle read;
    ScopedHandle write;
};

bool create_pipe(PipePair &pipe, std::string &error)
{
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE readHandle = nullptr;
    HANDLE writeHandle = nullptr;
    if (!::CreatePipe(&readHandle, &writeHandle, &security, 0))
    {
        error = windows_error("could not create process output pipe", ::GetLastError());
        return false;
    }
    pipe.read.reset(readHandle);
    pipe.write.reset(writeHandle);
    if (!::SetHandleInformation(pipe.read.get(), HANDLE_FLAG_INHERIT, 0))
    {
        error = windows_error("could not restrict process output pipe inheritance", ::GetLastError());
        return false;
    }
    return true;
}

class ScopedAttributeList
{
  public:
    ~ScopedAttributeList()
    {
        if (list_ != nullptr)
            ::DeleteProcThreadAttributeList(list_);
    }
    ScopedAttributeList(const ScopedAttributeList &) = delete;
    ScopedAttributeList &operator=(const ScopedAttributeList &) = delete;
    ScopedAttributeList() = default;

    bool initialize(std::string &error)
    {
        SIZE_T bytes = 0;
        ::InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        if (bytes == 0)
        {
            error = windows_error("could not size process attribute list", ::GetLastError());
            return false;
        }
        storage_.resize(bytes);
        list_ = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage_.data());
        if (!::InitializeProcThreadAttributeList(list_, 1, 0, &bytes))
        {
            list_ = nullptr;
            error = windows_error("could not initialize process attribute list", ::GetLastError());
            return false;
        }
        return true;
    }

    LPPROC_THREAD_ATTRIBUTE_LIST get() const noexcept { return list_; }

  private:
    std::vector<std::byte> storage_;
    LPPROC_THREAD_ATTRIBUTE_LIST list_ = nullptr;
};

void emit_output(WindowsProcessState &state, ProcessStream stream,
                 const char *data, std::size_t size)
{
    if (!state.output || size == 0)
        return;
    ProcessOutput event;
    event.generation = state.generation;
    event.stream = stream;
    event.bytes.assign(data, size);
    state.output(std::move(event));
}

void close_output_handle(HANDLE &handle) noexcept
{
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE)
        ::CloseHandle(handle);
    handle = nullptr;
}

void drain_pipe(WindowsProcessState &state, HANDLE &pipe, ProcessStream stream)
{
    if (pipe == nullptr)
        return;
    std::array<char, kReadChunkBytes> buffer{};
    std::size_t remaining = kMaxReadBytesPerPoll;
    while (remaining > 0)
    {
        DWORD available = 0;
        if (!::PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr))
        {
            const DWORD error = ::GetLastError();
            if (error != ERROR_BROKEN_PIPE && error != ERROR_PIPE_NOT_CONNECTED)
                append_error(state.ioError, windows_error("process output pipe query failed", error));
            close_output_handle(pipe);
            return;
        }
        if (available == 0)
            return;
        const DWORD requested = static_cast<DWORD>(std::min<std::size_t>(
            {remaining, buffer.size(), static_cast<std::size_t>(available)}));
        DWORD count = 0;
        if (!::ReadFile(pipe, buffer.data(), requested, &count, nullptr))
        {
            const DWORD error = ::GetLastError();
            if (error != ERROR_BROKEN_PIPE && error != ERROR_NO_DATA)
                append_error(state.ioError, windows_error("process output pipe read failed", error));
            close_output_handle(pipe);
            return;
        }
        if (count == 0)
        {
            close_output_handle(pipe);
            return;
        }
        emit_output(state, stream, buffer.data(), count);
        remaining -= count;
    }
}

void finish(WindowsProcessState &state)
{
    ProcessCompletion result;
    result.generation = state.generation;
    result.launched = true;
    result.exitCode = static_cast<int>(state.exitCode);
    result.error = std::move(state.ioError);

    auto callback = std::move(state.completion);
    close_output_handle(state.standardOutput);
    close_output_handle(state.standardError);
    close_output_handle(state.process);
    close_output_handle(state.job);
    state.processId = 0;
    state.generation = 0;
    state.exited = false;
    state.exitCode = STILL_ACTIVE;
    state.stopRequested = false;
    state.ioError.clear();
    if (callback)
        callback(std::move(result));
}

bool configure_job(ScopedHandle &job, std::string &error)
{
    ScopedHandle created(::CreateJobObjectW(nullptr, nullptr));
    if (created.get() == nullptr)
    {
        error = windows_error("could not create process ownership job", ::GetLastError());
        return false;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!::SetInformationJobObject(created.get(), JobObjectExtendedLimitInformation,
                                   &limits, sizeof(limits)))
    {
        error = windows_error("could not configure process ownership job", ::GetLastError());
        return false;
    }
    job = std::move(created);
    return true;
}

bool terminate_unresumed_process(HANDLE process, HANDLE job) noexcept
{
    if (job != nullptr)
        ::TerminateJobObject(job, ERROR_PROCESS_ABORTED);
    if (process != nullptr)
        ::TerminateProcess(process, ERROR_PROCESS_ABORTED);
    return process == nullptr || ::WaitForSingleObject(process, INFINITE) == WAIT_OBJECT_0;
}
} // namespace

void set_native_process_test_hooks(NativeProcessTestHooks hooks)
{
    std::lock_guard lock(testHooksMutex);
    testHooks = std::move(hooks);
}

void clear_native_process_test_hooks()
{
    std::lock_guard lock(testHooksMutex);
    testHooks = {};
}

namespace
{
class WindowsNativeProcess final : public NativeProcess
{
  public:
    bool start(ProcessRequest request, std::uint64_t generation,
               ProcessService::OutputCallback output,
               ProcessService::CompletionCallback completion) override
    {
        if (state_.process != nullptr)
        {
            report_start_failure(generation, completion, "another process is already running");
            return false;
        }
        if (request.argv.empty() || request.argv.front().empty())
        {
            report_start_failure(generation, completion, "process argv must contain a nonempty executable path");
            return false;
        }

        std::error_code filesystemError;
        if (request.workingDirectory.empty())
            request.workingDirectory = std::filesystem::current_path(filesystemError);
        if (filesystemError)
        {
            report_start_failure(generation, completion,
                                 "could not read current directory: " + filesystemError.message());
            return false;
        }
        request.workingDirectory = std::filesystem::absolute(request.workingDirectory, filesystemError);
        if (filesystemError || !std::filesystem::is_directory(request.workingDirectory, filesystemError) ||
            filesystemError)
        {
            report_start_failure(generation, completion,
                                 filesystemError ? filesystemError.message() : "working directory is not a directory");
            return false;
        }

        std::wstring application;
        std::wstring commandLine;
        std::string error;
        const auto hooks = current_test_hooks();
        if (!make_command_line(request.argv, commandLine, application,
                               request.workingDirectory, error))
        {
            report_start_failure(generation, completion, std::move(error));
            return false;
        }

        PipePair outputPipe;
        PipePair errorPipe;
        if (!create_pipe(outputPipe, error) ||
            (request.outputMode == ProcessOutputMode::Separate && !create_pipe(errorPipe, error)))
        {
            report_start_failure(generation, completion, std::move(error));
            return false;
        }

        SECURITY_ATTRIBUTES security{};
        security.nLength = sizeof(security);
        security.bInheritHandle = TRUE;
        ScopedHandle standardInput(::CreateFileW(L"NUL", GENERIC_READ,
                                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (standardInput.get() == INVALID_HANDLE_VALUE)
        {
            report_start_failure(generation, completion,
                                 windows_error("could not open child standard input", ::GetLastError()));
            return false;
        }

        ScopedAttributeList attributes;
        if (!attributes.initialize(error))
        {
            report_start_failure(generation, completion, std::move(error));
            return false;
        }
        HANDLE childOutput = outputPipe.write.get();
        HANDLE childError = request.outputMode == ProcessOutputMode::Merged
                                ? childOutput
                                : errorPipe.write.get();
        std::vector<HANDLE> inherited{standardInput.get(), childOutput};
        if (childError != childOutput)
            inherited.push_back(childError);
        if (request.sessionChannels != nullptr)
        {
            if (request.sessionChannels->childEndpoints.size() != 3)
            {
                report_start_failure(generation, completion,
                                     "session process launch requires exactly three inherited endpoints");
                return false;
            }
            for (const auto &endpoint : request.sessionChannels->childEndpoints)
            {
                const HANDLE handle = reinterpret_cast<HANDLE>(endpoint.nativeHandle);
                if (handle == nullptr || handle == INVALID_HANDLE_VALUE ||
                    std::find(inherited.begin(), inherited.end(), handle) != inherited.end())
                {
                    report_start_failure(generation, completion,
                                         "session process launch contains an invalid or duplicate handle");
                    return false;
                }
                inherited.push_back(handle);
            }
        }
        if (hooks.failHandleWhitelistUpdate ||
            !::UpdateProcThreadAttribute(attributes.get(), 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                         inherited.data(), inherited.size() * sizeof(HANDLE), nullptr, nullptr))
        {
            const DWORD setupError = hooks.failHandleWhitelistUpdate ? ERROR_ACCESS_DENIED : ::GetLastError();
            if (hooks.onHandleWhitelistFailure)
                hooks.onHandleWhitelistFailure();
            report_start_failure(generation, completion,
                                 windows_error("could not restrict inherited child handles", setupError));
            return false;
        }

        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput = standardInput.get();
        startup.StartupInfo.hStdOutput = childOutput;
        startup.StartupInfo.hStdError = childError;
        startup.lpAttributeList = attributes.get();

        ScopedHandle job;
        if (!configure_job(job, error))
        {
            report_start_failure(generation, completion, std::move(error));
            return false;
        }

        PROCESS_INFORMATION processInfo{};
        std::wstring workingDirectory = request.workingDirectory.native();
        const DWORD flags = CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT;
        if (!::CreateProcessW(application.c_str(), commandLine.data(), nullptr, nullptr, TRUE, flags,
                              nullptr, workingDirectory.c_str(), &startup.StartupInfo, &processInfo))
        {
            report_start_failure(generation, completion,
                                 windows_error("could not start process", ::GetLastError()));
            return false;
        }
        ScopedHandle childProcess(processInfo.hProcess);
        ScopedHandle childThread(processInfo.hThread);

        if (hooks.failJobAssignment || !::AssignProcessToJobObject(job.get(), childProcess.get()))
        {
            const DWORD setupError = hooks.failJobAssignment ? ERROR_ACCESS_DENIED : ::GetLastError();
            const bool reaped = terminate_unresumed_process(childProcess.get(), job.get());
            if (hooks.onSuspendedChildCleanup)
                hooks.onSuspendedChildCleanup(reaped);
            report_start_failure(generation, completion,
                                 windows_error("could not assign suspended process to ownership job", setupError));
            return false;
        }
        if (::ResumeThread(childThread.get()) == static_cast<DWORD>(-1))
        {
            const DWORD setupError = ::GetLastError();
            const bool reaped = terminate_unresumed_process(childProcess.get(), job.get());
            if (hooks.onSuspendedChildCleanup)
                hooks.onSuspendedChildCleanup(reaped);
            report_start_failure(generation, completion,
                                 windows_error("could not resume process after job assignment", setupError));
            return false;
        }

        if (request.sessionChannels != nullptr)
            request.sessionChannels->close_child_endpoints();

        outputPipe.write.reset();
        errorPipe.write.reset();
        state_.process = childProcess.release();
        state_.job = job.release();
        state_.standardOutput = outputPipe.read.release();
        state_.standardError = request.outputMode == ProcessOutputMode::Separate
                                  ? errorPipe.read.release()
                                  : nullptr;
        state_.processId = processInfo.dwProcessId;
        state_.outputMode = request.outputMode;
        state_.generation = generation;
        state_.output = std::move(output);
        state_.completion = std::move(completion);
        state_.exited = false;
        state_.exitCode = STILL_ACTIVE;
        state_.stopRequested = false;
        state_.ioError.clear();
        return true;
    }

    void poll() override
    {
        if (state_.process == nullptr)
            return;
        drain_pipe(state_, state_.standardOutput,
                   state_.outputMode == ProcessOutputMode::Merged ? ProcessStream::Merged : ProcessStream::Stdout);
        drain_pipe(state_, state_.standardError, ProcessStream::Stderr);
        if (!state_.exited)
        {
            const DWORD wait = ::WaitForSingleObject(state_.process, 0);
            if (wait == WAIT_OBJECT_0)
            {
                state_.exited = true;
                if (!::GetExitCodeProcess(state_.process, &state_.exitCode))
                    append_error(state_.ioError, windows_error("could not read process exit code", ::GetLastError()));
            }
            else if (wait == WAIT_FAILED)
                append_error(state_.ioError, windows_error("could not poll process", ::GetLastError()));
        }
        drain_pipe(state_, state_.standardOutput,
                   state_.outputMode == ProcessOutputMode::Merged ? ProcessStream::Merged : ProcessStream::Stdout);
        drain_pipe(state_, state_.standardError, ProcessStream::Stderr);
        if (state_.exited && state_.standardOutput == nullptr && state_.standardError == nullptr)
            finish(state_);
    }

    void stop() override
    {
        if (state_.process == nullptr || state_.stopRequested)
            return;
        state_.stopRequested = true;
        if (state_.job != nullptr && !::TerminateJobObject(state_.job, ERROR_CANCELLED))
            append_error(state_.ioError, windows_error("could not terminate owned process job", ::GetLastError()));
    }

    void shutdown() noexcept override
    {
        state_.output = {};
        state_.completion = {};
        stop();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
        while (state_.process != nullptr && std::chrono::steady_clock::now() < deadline)
        {
            poll();
            if (state_.process != nullptr)
                ::Sleep(2);
        }
        if (state_.process != nullptr)
            force_cleanup();
    }

    bool running() const noexcept override { return state_.process != nullptr; }
    int process_id() const noexcept override
    {
        return state_.process != nullptr && state_.processId <=
                   static_cast<DWORD>(std::numeric_limits<int>::max())
                   ? static_cast<int>(state_.processId)
                   : -1;
    }

  private:
    void force_cleanup() noexcept
    {
        if (state_.job != nullptr)
            ::TerminateJobObject(state_.job, ERROR_PROCESS_ABORTED);
        if (state_.process != nullptr)
        {
            ::TerminateProcess(state_.process, ERROR_PROCESS_ABORTED);
            ::WaitForSingleObject(state_.process, INFINITE);
        }
        close_output_handle(state_.standardOutput);
        close_output_handle(state_.standardError);
        close_output_handle(state_.process);
        close_output_handle(state_.job);
        state_.processId = 0;
        state_.generation = 0;
        state_.exited = false;
        state_.exitCode = STILL_ACTIVE;
    }

    WindowsProcessState state_;
};
} // namespace

std::unique_ptr<NativeProcess> make_native_process()
{
    return std::make_unique<WindowsNativeProcess>();
}

std::string_view native_process_containment_note() noexcept
{
    return "Windows processes launch suspended, enter a kill-on-close Job Object before resume, "
           "and inherit only the configured standard I/O handles; Stop terminates the owned job.";
}
} // namespace platformator_editor
