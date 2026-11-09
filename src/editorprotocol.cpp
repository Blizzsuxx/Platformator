#include "editorprotocol.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace platformator::editor_session
{
namespace
{
constexpr std::size_t FrameHeaderBytes = sizeof(std::uint32_t);

std::size_t message_limit(Direction direction) noexcept
{
    return direction == Direction::Request ? MaximumRequestBytes : MaximumEventBytes;
}

void append_u32_le(std::vector<std::uint8_t> &output, std::uint32_t value)
{
    output.push_back(static_cast<std::uint8_t>(value & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
}

std::uint32_t read_u32_le(const std::uint8_t *bytes) noexcept
{
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

bool validate_message(const Message &message, Direction direction, std::string &error)
{
    if (message.protocolVersion != ProtocolVersion)
        error = "unsupported editor-session protocol version";
    else if (message.nonce.empty() || message.nonce.size() > 128)
        error = "session nonce is missing or exceeds 128 bytes";
    else if (message.playGeneration == 0 || message.sequence == 0)
        error = "play generation and message sequence must be positive";
    else if (direction == Direction::Event && message.runtimeSerial == 0)
        error = "runtime events require a positive runtime serial";
    else if (direction == Direction::Request && message.runtimeSerial != 0)
        error = "requests cannot claim a runtime serial";
    else if (message.type.empty() || message.type.size() > 64)
        error = "message type is missing or exceeds 64 bytes";
    else if (!message.body.is_object())
        error = "message body must be a JSON object";
    else
        return true;
    return false;
}

bool read_optional_bool(const nlohmann::json &source, const char *key, bool &value,
                        std::string &error)
{
    const auto found = source.find(key);
    if (found == source.end())
        return true;
    if (!found->is_boolean())
    {
        error = std::string("editor capability '") + key + "' must be boolean";
        return false;
    }
    value = found->get<bool>();
    return true;
}
} // namespace

std::vector<std::uint8_t> encode_frame(const Message &message, Direction direction,
                                      std::string &error)
{
    error.clear();
    if (!validate_message(message, direction, error))
        return {};
    nlohmann::json document = {
        {"protocol_version", message.protocolVersion},
        {"nonce", message.nonce},
        {"play_generation", message.playGeneration},
        {"sequence", message.sequence},
        {"type", message.type},
        {"body", message.body}};
    if (direction == Direction::Event)
        document["runtime_serial"] = message.runtimeSerial;
    const std::string payload = document.dump();
    if (payload.size() > message_limit(direction))
    {
        error = "encoded message exceeds the direction-specific byte limit";
        return {};
    }
    std::vector<std::uint8_t> frame;
    frame.reserve(FrameHeaderBytes + payload.size());
    append_u32_le(frame, static_cast<std::uint32_t>(payload.size()));
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

DecodeResult StreamDecoder::feed(std::span<const std::uint8_t> bytes)
{
    DecodeResult result;
    if (failed_)
    {
        result.error = "editor-session decoder is already failed";
        return result;
    }

    std::size_t offset = 0;
    while (offset < bytes.size())
    {
        if (frame_.size() < FrameHeaderBytes)
        {
            const std::size_t count = std::min(FrameHeaderBytes - frame_.size(), bytes.size() - offset);
            frame_.insert(frame_.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                          bytes.begin() + static_cast<std::ptrdiff_t>(offset + count));
            offset += count;
            if (frame_.size() < FrameHeaderBytes)
                break;

            const auto payloadBytes = read_u32_le(frame_.data());
            if (payloadBytes == 0 || payloadBytes > message_limit(direction_))
            {
                failed_ = true;
                frame_.clear();
                result.error = "frame length is zero or exceeds the protocol limit";
                return result;
            }
            expectedFrameBytes_ = FrameHeaderBytes + static_cast<std::size_t>(payloadBytes);
        }

        if (frame_.size() < expectedFrameBytes_ && offset < bytes.size())
        {
            const std::size_t count = std::min(expectedFrameBytes_ - frame_.size(), bytes.size() - offset);
            frame_.insert(frame_.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                          bytes.begin() + static_cast<std::ptrdiff_t>(offset + count));
            offset += count;
        }
        if (frame_.size() < expectedFrameBytes_)
            break;

        try
        {
            const char *payload = reinterpret_cast<const char *>(frame_.data() + FrameHeaderBytes);
            const std::size_t payloadBytes = expectedFrameBytes_ - FrameHeaderBytes;
            const auto document = nlohmann::json::parse(payload, payload + payloadBytes);
            if (!document.is_object())
                throw std::runtime_error("message envelope must be a JSON object");

            Message message;
            const auto version = document.find("protocol_version");
            const auto nonce = document.find("nonce");
            const auto generation = document.find("play_generation");
            const auto sequence = document.find("sequence");
            const auto type = document.find("type");
            const auto body = document.find("body");
            if (version == document.end() || !version->is_number_unsigned() ||
                nonce == document.end() || !nonce->is_string() ||
                generation == document.end() || !generation->is_number_unsigned() ||
                sequence == document.end() || !sequence->is_number_unsigned() ||
                type == document.end() || !type->is_string() ||
                body == document.end() || !body->is_object())
                throw std::runtime_error("message envelope is missing a field or has an invalid field type");
            message.protocolVersion = version->get<std::uint32_t>();
            message.nonce = nonce->get<std::string>();
            message.playGeneration = generation->get<std::uint64_t>();
            message.sequence = sequence->get<std::uint64_t>();
            message.type = type->get<std::string>();
            message.body = *body;
            if (direction_ == Direction::Event)
            {
                const auto serial = document.find("runtime_serial");
                if (serial == document.end() || !serial->is_number_unsigned())
                    throw std::runtime_error("runtime event is missing its runtime serial");
                message.runtimeSerial = serial->get<std::uint64_t>();
            }
            else if (document.contains("runtime_serial"))
                throw std::runtime_error("request cannot contain a runtime serial");
            std::string validationError;
            if (!validate_message(message, direction_, validationError))
                throw std::runtime_error(validationError);
            result.messages.push_back(std::move(message));
        }
        catch (const std::exception &error)
        {
            failed_ = true;
            frame_.clear();
            expectedFrameBytes_ = 0;
            result.messages.clear();
            result.error = std::string("invalid editor-session frame: ") + error.what();
            return result;
        }
        frame_.clear();
        expectedFrameBytes_ = 0;
    }
    return result;
}

DecodeResult StreamDecoder::finish()
{
    DecodeResult result;
    if (failed_)
    {
        result.error = "editor-session decoder is already failed";
        return result;
    }
    if (!frame_.empty())
    {
        failed_ = true;
        frame_.clear();
        expectedFrameBytes_ = 0;
        result.error = "truncated editor-session frame at end of stream";
    }
    return result;
}

void StreamDecoder::reset() noexcept
{
    frame_.clear();
    expectedFrameBytes_ = 0;
    failed_ = false;
}

SessionCapabilities read_session_capabilities(const nlohmann::json &catalog)
{
    SessionCapabilities result;
    if (!catalog.is_object() || !catalog.contains("schema_version") ||
        !catalog["schema_version"].is_number_integer() || catalog["schema_version"] != 1 ||
        !catalog.contains("behaviors") || !catalog["behaviors"].is_array())
    {
        result.error = "catalog envelope must use schema version 1 and contain behaviors";
        return result;
    }
    if (!catalog.contains("editor_capabilities"))
        return result;
    const auto &capabilities = catalog["editor_capabilities"];
    if (!capabilities.is_object())
    {
        result.error = "editor_capabilities must be an object";
        return result;
    }
    const auto protocol = capabilities.find("session_protocol");
    if (protocol == capabilities.end() || !protocol->is_string() ||
        protocol->get<std::string>() != "platformator-editor-session-v1")
    {
        result.error = "session_protocol must identify platformator-editor-session-v1";
        return result;
    }
    if (!read_optional_bool(capabilities, "play_control", result.playControl, result.error) ||
        !read_optional_bool(capabilities, "runtime_inspection", result.runtimeInspection, result.error) ||
        !read_optional_bool(capabilities, "live_asset_reload", result.liveAssetReload, result.error) ||
        !read_optional_bool(capabilities, "input_forwarding", result.inputForwarding, result.error))
        return result;
    if (result.inputForwarding)
    {
        result.error = "editor-session v1 requires input_forwarding=false";
        return result;
    }
    if (capabilities.contains("rgba_frame_stream"))
    {
        const auto &frame = capabilities["rgba_frame_stream"];
        if (!frame.is_object() || !frame.contains("version") || !frame["version"].is_number_integer() ||
            frame["version"] != 1 || !frame.contains("width") || !frame["width"].is_number_integer() ||
            frame["width"] != 640 || !frame.contains("height") || !frame["height"].is_number_integer() ||
            frame["height"] != 360 || !frame.contains("max_hz") || !frame["max_hz"].is_number_integer() ||
            frame["max_hz"] <= 0 || frame["max_hz"] > 15)
        {
            result.error = "RGBA frame capability must be v1, 640x360, and at most 15 Hz";
            return result;
        }
        result.frameStream = true;
    }
    result.supported = true;
    return result;
}

bool BoundedQueue::enqueue(const Message &message, bool reliableEvent)
{
    lastError_.clear();
    if (reliableEvent && direction_ != Direction::Event)
    {
        lastError_ = "reliable-event reserve is available only on the event queue";
        return false;
    }
    std::string error;
    auto frame = encode_frame(message, direction_, error);
    if (!error.empty())
    {
        lastError_ = std::move(error);
        return false;
    }
    const std::size_t limit = direction_ == Direction::Event && !reliableEvent
        ? MaximumQueuedBytes - ReliableEventReserveBytes : MaximumQueuedBytes;
    if (frames_.size() >= MaximumQueuedMessages || frame.size() > limit - std::min(bytes_, limit))
    {
        lastError_ = "bounded editor-session queue is full";
        return false;
    }
    bytes_ += frame.size();
    frames_.push_back(std::move(frame));
    return true;
}

bool BoundedQueue::pop(std::vector<std::uint8_t> &frame)
{
    if (frames_.empty())
        return false;
    const auto &frontFrame = frames_.front();
    frame.assign(frontFrame.begin() + static_cast<std::ptrdiff_t>(frontOffset_),
                 frontFrame.end());
    frames_.pop_front();
    bytes_ -= frame.size();
    frontOffset_ = 0;
    return true;
}

std::span<const std::uint8_t> BoundedQueue::front() const noexcept
{
    if (frames_.empty())
        return {};
    const auto &frame = frames_.front();
    return std::span<const std::uint8_t>(frame).subspan(frontOffset_);
}

bool BoundedQueue::consume(std::size_t bytes) noexcept
{
    if (frames_.empty() || bytes > frames_.front().size() - frontOffset_)
        return false;
    frontOffset_ += bytes;
    bytes_ -= bytes;
    if (frontOffset_ == frames_.front().size())
    {
        frames_.pop_front();
        frontOffset_ = 0;
    }
    return true;
}

bool SequenceTracker::accept(std::uint64_t sequence) noexcept
{
    if (sequence == 0 || sequence <= last_)
        return false;
    last_ = sequence;
    return true;
}

bool InspectionPageGate::try_begin() noexcept
{
    if (inFlight_)
        return false;
    inFlight_ = true;
    return true;
}
} // namespace platformator::editor_session
