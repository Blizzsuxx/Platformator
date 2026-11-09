#include <editorprotocol.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <span>
#include <thread>
#include <vector>

#include <sys/mman.h>
#include <unistd.h>

namespace
{
using namespace platformator::editor_session;
using namespace std::chrono_literals;

constexpr std::size_t MappingBytes = 1'843'200;

constexpr char Catalog[] =
    R"({"schema_version":1,"behaviors":[],"editor_capabilities":{"session_protocol":"platformator-editor-session-v1","play_control":true,"runtime_inspection":true,"rgba_frame_stream":{"version":1,"width":640,"height":360,"max_hz":15},"live_asset_reload":false,"input_forwarding":false}})";

bool write_event(const Message &message)
{
    std::string error;
    const auto frame = encode_frame(message, Direction::Event, error);
    if (!error.empty())
        return false;
    std::size_t offset = 0;
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (offset < frame.size() && std::chrono::steady_clock::now() < deadline)
    {
        const auto count = ::write(4, frame.data() + offset, frame.size() - offset);
        if (count > 0)
        {
            offset += static_cast<std::size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            std::this_thread::sleep_for(1ms);
            continue;
        }
        return false;
    }
    return offset == frame.size();
}

bool write_handshake(const Message &request, bool frames)
{
    Message capabilities;
    capabilities.nonce = request.nonce;
    capabilities.playGeneration = request.playGeneration;
    capabilities.sequence = 1;
    capabilities.runtimeSerial = 44;
    capabilities.type = "capabilities";
    capabilities.body = {{"session_protocol", "platformator-editor-session-v1"},
                         {"play_control", true}, {"runtime_inspection", true},
                         {"live_asset_reload", false}, {"input_forwarding", false}};
    if (frames)
        capabilities.body["rgba_frame_stream"] = {{"version", 1}, {"width", 640},
                                                   {"height", 360}, {"max_hz", 15}};
    Message ready = capabilities;
    ready.sequence = 2;
    ready.type = "ready";
    return write_event(capabilities) && write_event(ready);
}

int run_fixture(bool frames)
{
    void *mapping = ::mmap(nullptr, MappingBytes, PROT_READ | PROT_WRITE, MAP_SHARED, 5, 0);
    if (mapping == MAP_FAILED)
        return 61;
    static_cast<std::byte *>(mapping)[0] = std::byte{0x5a};
    constexpr char stdoutText[] = "play-session-stdout\n";
    constexpr char stderrText[] = "play-session-stderr\n";
    if (::write(STDOUT_FILENO, stdoutText, sizeof(stdoutText) - 1) !=
            static_cast<ssize_t>(sizeof(stdoutText) - 1) ||
        ::write(STDERR_FILENO, stderrText, sizeof(stderrText) - 1) !=
            static_cast<ssize_t>(sizeof(stderrText) - 1))
    {
        ::munmap(mapping, MappingBytes);
        return 62;
    }

    StreamDecoder decoder(Direction::Request);
    bool helloAccepted = false;
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    std::array<std::uint8_t, 16U * 1024U> buffer{};
    while (std::chrono::steady_clock::now() < deadline)
    {
        const auto count = ::read(3, buffer.data(), buffer.size());
        if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            break;
        if (count > 0)
        {
            const auto decoded = decoder.feed(std::span<const std::uint8_t>(buffer).first(
                static_cast<std::size_t>(count)));
            if (!decoded.succeeded())
                break;
            for (const auto &request : decoded.messages)
            {
                if (!helloAccepted)
                {
                    if (request.type != "hello" || request.sequence != 1 ||
                        !write_handshake(request, frames))
                        break;
                    helloAccepted = true;
                    continue;
                }
                if (request.type == "stop")
                {
                    Message stopped;
                    stopped.nonce = request.nonce;
                    stopped.playGeneration = request.playGeneration;
                    stopped.sequence = 3;
                    stopped.runtimeSerial = 44;
                    stopped.type = "stopped";
                    const bool wrote = write_event(stopped);
                    ::munmap(mapping, MappingBytes);
                    return wrote ? 0 : 63;
                }
            }
        }
        else if (count == 0)
        {
            const auto finished = decoder.finish();
            (void)finished;
            break;
        }
        std::this_thread::sleep_for(1ms);
    }
    ::munmap(mapping, MappingBytes);
    return helloAccepted ? 64 : 65;
}
} // namespace

int main(int argc, char **argv)
{
    if (argc >= 2 && std::string_view(argv[1]) == "crash")
    {
        ::raise(SIGKILL);
        return 67;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--editor-catalog")
    {
        std::cout << Catalog << '\n';
        return std::cout ? 0 : 66;
    }
    if (argc == 3 && std::string_view(argv[1]) == "args" &&
        (std::string_view(argv[2]) == "configure" || std::string_view(argv[2]) == "build"))
        return 0;
    bool frameStreamRequested = false;
    for (int index = 1; index < argc; ++index)
        frameStreamRequested |= std::string_view(argv[index]) == "--scene";
    return run_fixture(frameStreamRequested);
}
