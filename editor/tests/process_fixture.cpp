#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <string>
#include <string_view>
#include <vector>

namespace
{
std::wstring executable_path()
{
    std::vector<wchar_t> buffer(512);
    for (;;)
    {
        const DWORD length = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0)
            return {};
        if (length < buffer.size() - 1)
            return std::wstring(buffer.data(), length);
        buffer.resize(buffer.size() * 2);
    }
}

std::string utf8(std::wstring_view text)
{
    if (text.empty())
        return {};
    const int size = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                                          static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    if (::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                              static_cast<int>(text.size()), result.data(), size, nullptr, nullptr) != size)
        return {};
    return result;
}

int write_all(HANDLE handle, const char *data, std::size_t size)
{
    while (size > 0)
    {
        const DWORD requested = static_cast<DWORD>(std::min<std::size_t>(size, 1u << 20));
        DWORD written = 0;
        if (!::WriteFile(handle, data, requested, &written, nullptr) || written == 0)
            return 1;
        data += written;
        size -= written;
    }
    return 0;
}

std::wstring quote_argument(std::wstring_view value)
{
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (wchar_t ch : value)
    {
        if (ch == L'\\')
        {
            ++slashes;
            continue;
        }
        if (ch == L'\"')
        {
            result.append(slashes * 2 + 1, L'\\');
            result.push_back(ch);
            slashes = 0;
            continue;
        }
        result.append(slashes, L'\\');
        slashes = 0;
        result.push_back(ch);
    }
    result.append(slashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}

int write_wide(HANDLE handle, std::wstring_view text)
{
    const auto encoded = utf8(text);
    return encoded.size() == 0 && !text.empty() ? 2 : write_all(handle, encoded.data(), encoded.size());
}

int write_marker_file(const wchar_t *path)
{
    HANDLE file = ::CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return 30;
    constexpr char marker[] = "executed\n";
    DWORD written = 0;
    const BOOL ok = ::WriteFile(file, marker, sizeof(marker) - 1, &written, nullptr);
    ::CloseHandle(file);
    return ok && written == sizeof(marker) - 1 ? 0 : 31;
}

int launch_hanging_child()
{
    const auto executable = executable_path();
    if (executable.empty())
        return 10;
    std::wstring commandLine = quote_argument(executable) + L" hang";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    if (!::CreateProcessW(executable.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
                          CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child))
        return 11;
    const std::wstring pid = L"grandchild:" + std::to_wstring(child.dwProcessId) + L"\n";
    ::CloseHandle(child.hThread);
    ::CloseHandle(child.hProcess);
    return write_wide(::GetStdHandle(STD_OUTPUT_HANDLE), pid);
}

HANDLE session_handle(int argc, wchar_t **argv, const wchar_t *name)
{
    for (int index = 2; index + 1 < argc; ++index)
        if (std::wstring_view(argv[index]) == name)
        {
            const auto value = static_cast<std::uintptr_t>(std::wcstoull(argv[index + 1], nullptr, 10));
            return reinterpret_cast<HANDLE>(value);
        }
    return nullptr;
}

int probe_session_channels(int argc, wchar_t **argv)
{
    HANDLE commands = session_handle(argc, argv, L"--editor-session-command-handle");
    HANDLE events = session_handle(argc, argv, L"--editor-session-event-handle");
    HANDLE mapping = session_handle(argc, argv, L"--editor-session-frame-handle");
    if (commands == nullptr || events == nullptr || mapping == nullptr)
        return 40;

    char command = 0;
    DWORD count = 0;
    if (!::ReadFile(commands, &command, 1, &count, nullptr) || count != 1 || command != 'C')
        return 41;
    void *view = ::MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 1);
    if (view == nullptr)
        return 42;
    static_cast<unsigned char *>(view)[0] = 0x5a;
    ::UnmapViewOfFile(view);
    const char event = 'E';
    if (!::WriteFile(events, &event, 1, &count, nullptr) || count != 1)
        return 43;
    return 0;
}
} // namespace

int wmain(int argc, wchar_t **argv)
{
    if (argc < 2)
        return 2;
    const std::wstring_view mode(argv[1]);
    HANDLE standardOutput = ::GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE standardError = ::GetStdHandle(STD_ERROR_HANDLE);
    if (mode == L"--editor-catalog")
    {
#ifdef PLATFORMATOR_EDITOR_INVALID_CATALOG
        constexpr char catalog[] = "this target does not implement common-runner-v1";
#else
        constexpr char catalog[] =
            "{\"schema_version\":1,\"behaviors\":[{\"name\":\"Allowed\","
            "\"field_schema_available\":true,\"fields\":[]}]}";
#endif
        return write_all(standardOutput, catalog, sizeof(catalog) - 1) == 0 ? 0 : 13;
    }
    if (mode == L"args")
    {
        DWORD size = ::GetCurrentDirectoryW(0, nullptr);
        std::vector<wchar_t> cwd(size);
        if (size == 0 || ::GetCurrentDirectoryW(size, cwd.data()) == 0)
            return 3;
        if (write_wide(standardOutput, L"cwd:" + std::wstring(cwd.data()) + L"\n") != 0)
            return 4;
        for (int index = 2; index < argc; ++index)
            if (write_wide(standardOutput, L"arg:" + std::wstring(argv[index]) + L"\n") != 0)
                return 5;
        return 0;
    }
    if (mode == L"session")
        return probe_session_channels(argc, argv);
    if (mode == L"large")
    {
        constexpr std::size_t chunkSize = 4096;
        constexpr std::size_t chunkCount = 512;
        const std::string chunk(chunkSize, 'x');
        for (std::size_t index = 0; index < chunkCount; ++index)
            if (write_all(standardOutput, chunk.data(), chunk.size()) != 0)
                return 6;
        constexpr char marker[] = "large-output-complete\n";
        return write_all(standardOutput, marker, sizeof(marker) - 1) == 0 ? 0 : 7;
    }
    if (mode == L"streams")
    {
        constexpr char out[] = "stdout-marker\n";
        constexpr char err[] = "stderr-marker\n";
        if (write_all(standardOutput, out, sizeof(out) - 1) != 0)
            return 8;
        if (write_all(standardError, err, sizeof(err) - 1) != 0)
            return 9;
        return 0;
    }
    if (mode == L"--write-marker" && argc == 3)
        return write_marker_file(argv[2]);
    if (mode == L"--check-handle" && argc == 3)
    {
        const auto value = static_cast<std::uintptr_t>(std::wcstoull(argv[2], nullptr, 10));
        DWORD flags = 0;
        return ::GetHandleInformation(reinterpret_cast<HANDLE>(value), &flags) ? 32 : 0;
    }
    if (mode == L"grandchild")
    {
        if (launch_hanging_child() != 0)
            return 12;
    }
    if (mode == L"hang" || mode == L"grandchild" || mode == L"ignore-term" || mode == L"daemon-escape")
    {
        const std::wstring status = L"pid:" + std::to_wstring(::GetCurrentProcessId()) +
                                    L" group:0 grandchild:0\n";
        if (write_wide(standardOutput, status) != 0)
            return 14;
        ::Sleep(INFINITE);
    }
    if (mode == L"fail")
        return 23;
    return 12;
}

#else
#include <cstdio>
#include <csignal>
#include <cstring>
#include <string>

#include <sys/types.h>
#include <unistd.h>

namespace
{
int write_all(int fd, const char *data, std::size_t size)
{
    while (size > 0)
    {
        const ssize_t written = ::write(fd, data, size);
        if (written <= 0)
            return 1;
        data += written;
        size -= static_cast<std::size_t>(written);
    }
    return 0;
}
} // namespace

int main(int argc, char **argv)
{
    if (argc < 2)
        return 2;
    const std::string mode = argv[1];
    if (mode == "--editor-catalog")
    {
#ifdef PLATFORMATOR_EDITOR_INVALID_CATALOG
        constexpr char catalog[] = "this target does not implement common-runner-v1";
#else
        constexpr char catalog[] =
            "{\"schema_version\":1,\"behaviors\":[{\"name\":\"Allowed\","
            "\"field_schema_available\":true,\"fields\":[]}]}";
#endif
        return write_all(STDOUT_FILENO, catalog, sizeof(catalog) - 1) == 0 ? 0 : 13;
    }
    if (mode == "args")
    {
        char cwd[4096]{};
        if (::getcwd(cwd, sizeof(cwd)) == nullptr)
            return 3;
        std::string line = "cwd:" + std::string(cwd) + "\n";
        if (write_all(STDOUT_FILENO, line.data(), line.size()) != 0)
            return 4;
        for (int i = 2; i < argc; ++i)
        {
            line = "arg:" + std::string(argv[i]) + "\n";
            if (write_all(STDOUT_FILENO, line.data(), line.size()) != 0)
                return 5;
        }
        return 0;
    }
    if (mode == "large")
    {
        constexpr std::size_t chunkSize = 4096;
        constexpr std::size_t chunkCount = 512;
        const std::string chunk(chunkSize, 'x');
        for (std::size_t i = 0; i < chunkCount; ++i)
            if (write_all(STDOUT_FILENO, chunk.data(), chunk.size()) != 0)
                return 6;
        constexpr char marker[] = "large-output-complete\n";
        return write_all(STDOUT_FILENO, marker, sizeof(marker) - 1) == 0 ? 0 : 7;
    }
    if (mode == "streams")
    {
        constexpr char out[] = "stdout-marker\n";
        constexpr char err[] = "stderr-marker\n";
        if (write_all(STDOUT_FILENO, out, sizeof(out) - 1) != 0)
            return 8;
        if (write_all(STDERR_FILENO, err, sizeof(err) - 1) != 0)
            return 9;
        return 0;
    }
    if (mode == "hang" || mode == "grandchild" || mode == "ignore-term" || mode == "daemon-escape")
    {
        if (mode == "ignore-term")
            ::signal(SIGTERM, SIG_IGN);
        pid_t child = -1;
        if (mode == "grandchild" || mode == "daemon-escape")
        {
            child = ::fork();
            if (child < 0)
                return 10;
            if (child == 0)
            {
                if (mode == "daemon-escape")
                {
                    if (::setsid() < 0)
                        _exit(14);
                    ::close(STDOUT_FILENO);
                    ::close(STDERR_FILENO);
                }
                for (;;)
                    ::pause();
            }
        }
        const std::string status = "pid:" + std::to_string(::getpid()) + " group:" +
                                   std::to_string(::getpgrp()) + " grandchild:" +
                                   std::to_string(child) + "\n";
        if (write_all(STDOUT_FILENO, status.data(), status.size()) != 0)
            return 11;
        for (;;)
            ::pause();
    }
    if (mode == "fail")
        return 23;
    return 12;
}
#endif
