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
    if (mode == "hang" || mode == "grandchild" || mode == "ignore-term")
    {
        if (mode == "ignore-term")
            ::signal(SIGTERM, SIG_IGN);
        pid_t child = -1;
        if (mode == "grandchild")
        {
            child = ::fork();
            if (child < 0)
                return 10;
            if (child == 0)
            {
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
