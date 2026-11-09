#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace platformator_editor
{
inline constexpr std::size_t session_frame_mapping_limit = 1'843'200;

enum class SessionChannelStatus
{
    Progress,
    WouldBlock,
    Closed,
    Cancelled,
    Failed
};

struct SessionChannelResult
{
    SessionChannelStatus status = SessionChannelStatus::WouldBlock;
    std::size_t bytesTransferred = 0;
    std::string error;
};

class SessionChannel
{
  public:
    virtual ~SessionChannel() = default;
    // Reads and writes are nonblocking. cancel() closes immediately and makes
    // subsequent calls return Cancelled; close() closes normally and returns Closed.
    virtual SessionChannelResult read(std::span<std::byte> destination) = 0;
    virtual SessionChannelResult write(std::span<const std::byte> source) = 0;
    virtual void cancel() noexcept = 0;
    virtual void close() noexcept = 0;
};

class SessionMapping
{
  public:
    virtual ~SessionMapping() = default;
    // The pair owns the mapping; callers close it only after event/frame consumers drain.
    virtual std::span<std::byte> bytes() noexcept = 0;
    virtual std::size_t size() const noexcept = 0;
    virtual void close() noexcept = 0;
};

struct InheritedSessionEndpoint
{
    std::string argument;
    std::string opaqueValue;
    std::uintptr_t nativeHandle = 0;
};

struct SessionChannelPair
{
    std::unique_ptr<SessionChannel> commands;
    std::unique_ptr<SessionChannel> events;
    std::unique_ptr<SessionMapping> frameMapping;
    std::vector<InheritedSessionEndpoint> childEndpoints;
    std::function<void(std::uintptr_t)> closeNativeHandle;

    // Call after the child has inherited these endpoints. The parent must not
    // retain duplicate child ends, because those duplicates suppress EOF/EPIPE.
    ~SessionChannelPair() { close_child_endpoints(); }
    SessionChannelPair() = default;
    SessionChannelPair(const SessionChannelPair &) = delete;
    SessionChannelPair &operator=(const SessionChannelPair &) = delete;

    void close_child_endpoints() noexcept
    {
        if (!closeNativeHandle)
        {
            for (auto &endpoint : childEndpoints)
                endpoint.nativeHandle = 0;
            return;
        }
        for (auto &endpoint : childEndpoints)
        {
            if (endpoint.nativeHandle != 0)
                closeNativeHandle(endpoint.nativeHandle);
            endpoint.nativeHandle = 0;
        }
    }
};

class SessionChannelProvider
{
  public:
    virtual ~SessionChannelProvider() = default;
    virtual std::unique_ptr<SessionChannelPair> create_pair(std::size_t mappingBytes,
                                                            std::string &error) = 0;
};
} // namespace platformator_editor
