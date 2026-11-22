#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <json.hpp>

namespace platformator::editor_session
{
inline constexpr std::uint32_t ProtocolVersion = 1;
inline constexpr std::size_t MaximumRequestBytes = 64U * 1024U;
inline constexpr std::size_t MaximumEventBytes = 256U * 1024U;
inline constexpr std::size_t MaximumQueuedMessages = 64;
inline constexpr std::size_t MaximumQueuedBytes = 1024U * 1024U;
inline constexpr std::size_t ReliableEventReserveBytes = 16U * 1024U;
inline constexpr std::size_t MaximumInspectionPageBytes = MaximumEventBytes;

enum class Direction
{
    Request,
    Event
};

enum class EmbeddedInputKind
{
    KeyDown,
    KeyUp,
    MouseMove,
    MouseButtonDown,
    MouseButtonUp,
    ReleaseAll
};

struct EmbeddedInputEvent
{
    EmbeddedInputKind kind = EmbeddedInputKind::ReleaseAll;
    int scancode = 0;                 // SDL_Scancode for KeyDown / KeyUp
    int keycode = 0;                  // SDL_Keycode for KeyDown / KeyUp
    std::uint16_t modifiers = 0;      // SDL_Keymod bitmask
    int mouseButton = 0;              // SDL mouse button index (1=Left, 2=Middle, 3=Right)
    float logicalX = 0.0f;            // X in runtime logical coordinates [0, width)
    float logicalY = 0.0f;            // Y in runtime logical coordinates [0, height)
};

struct Message
{
    std::uint32_t protocolVersion = ProtocolVersion;
    std::string nonce;
    std::uint64_t playGeneration = 0;
    std::uint64_t sequence = 0;
    std::uint64_t runtimeSerial = 0;
    std::string type;
    nlohmann::json body = nlohmann::json::object();
};

struct DecodeResult
{
    std::vector<Message> messages;
    std::string error;

    bool succeeded() const noexcept { return error.empty(); }
};

std::vector<std::uint8_t> encode_frame(const Message &message, Direction direction,
                                      std::string &error);

// Accepts arbitrary partial or coalesced reads. The length is checked against
// the direction-specific cap before any payload bytes are copied into storage.
class StreamDecoder
{
  public:
    explicit StreamDecoder(Direction direction) noexcept : direction_(direction) {}

    DecodeResult feed(std::span<const std::uint8_t> bytes);
    DecodeResult finish();
    bool failed() const noexcept { return failed_; }
    void reset() noexcept;

  private:
    Direction direction_;
    std::vector<std::uint8_t> frame_;
    std::size_t expectedFrameBytes_ = 0;
    bool failed_ = false;
};

struct SessionCapabilities
{
    bool supported = false;
    bool playControl = false;
    bool runtimeInspection = false;
    bool frameStream = false;
    bool liveAssetReload = false;
    bool inputForwarding = false;
    std::string error;
};

SessionCapabilities read_session_capabilities(const nlohmann::json &catalog);

class BoundedQueue
{
  public:
    explicit BoundedQueue(Direction direction) noexcept : direction_(direction) {}

    bool enqueue(const Message &message, bool reliableEvent = false);
    bool pop(std::vector<std::uint8_t> &frame);
    std::span<const std::uint8_t> front() const noexcept;
    bool consume(std::size_t bytes) noexcept;
    std::size_t size() const noexcept { return frames_.size(); }
    std::size_t bytes() const noexcept { return bytes_; }
    std::string_view last_error() const noexcept { return lastError_; }

  private:
    Direction direction_;
    std::deque<std::vector<std::uint8_t>> frames_;
    std::size_t frontOffset_ = 0;
    std::size_t bytes_ = 0;
    std::string lastError_;
};

class SequenceTracker
{
  public:
    bool accept(std::uint64_t sequence) noexcept;
    std::uint64_t last() const noexcept { return last_; }
    void reset() noexcept { last_ = 0; }

  private:
    std::uint64_t last_ = 0;
};

class InspectionPageGate
{
  public:
    bool try_begin() noexcept;
    void complete() noexcept { inFlight_ = false; }
    bool in_flight() const noexcept { return inFlight_; }

  private:
    bool inFlight_ = false;
};
} // namespace platformator::editor_session
