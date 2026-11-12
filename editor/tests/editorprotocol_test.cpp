#include "editorprotocol.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

using namespace platformator::editor_session;

namespace
{
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

Message request(std::uint64_t sequence, nlohmann::json body = nlohmann::json::object())
{
    return Message{ProtocolVersion, "0123456789abcdef", 7, sequence, 0, "hello", std::move(body)};
}

Message event(std::uint64_t sequence, nlohmann::json body = nlohmann::json::object())
{
    return Message{ProtocolVersion, "0123456789abcdef", 7, sequence, 23, "state", std::move(body)};
}

void test_partial_and_coalesced_frames()
{
    std::string error;
    const auto first = encode_frame(request(1), Direction::Request, error);
    const auto second = encode_frame(request(2, {{"value", 3}}), Direction::Request, error);
    require(error.empty() && !first.empty() && !second.empty(), "valid requests must encode");

    StreamDecoder decoder(Direction::Request);
    auto decoded = decoder.feed(std::span(first).first(2));
    require(decoded.succeeded() && decoded.messages.empty(), "partial length prefix must be buffered");
    decoded = decoder.feed(std::span(first).subspan(2));
    require(decoded.succeeded() && decoded.messages.size() == 1 &&
                decoded.messages.front().sequence == 1 && decoded.messages.front().type == "hello",
            "a completed request must restore its validated envelope");

    std::vector<std::uint8_t> coalesced = first;
    coalesced.insert(coalesced.end(), second.begin(), second.end());
    decoder.reset();
    decoded = decoder.feed(coalesced);
    require(decoded.succeeded() && decoded.messages.size() == 2 &&
                decoded.messages[1].sequence == 2 && decoded.messages[1].body["value"] == 3,
            "one pipe read may contain multiple complete requests");

    auto runtimeEvent = encode_frame(event(1), Direction::Event, error);
    StreamDecoder events(Direction::Event);
    decoded = events.feed(runtimeEvent);
    require(decoded.succeeded() && decoded.messages.size() == 1 &&
                decoded.messages.front().runtimeSerial == 23,
            "runtime events must preserve their runtime serial");
}

void test_rejects_bad_and_truncated_frames()
{
    StreamDecoder oversized(Direction::Request);
    const std::uint32_t claimed = static_cast<std::uint32_t>(MaximumRequestBytes + 1);
    const std::vector<std::uint8_t> header{
        static_cast<std::uint8_t>(claimed & 0xffU),
        static_cast<std::uint8_t>((claimed >> 8U) & 0xffU),
        static_cast<std::uint8_t>((claimed >> 16U) & 0xffU),
        static_cast<std::uint8_t>((claimed >> 24U) & 0xffU)};
    auto decoded = oversized.feed(header);
    require(!decoded.succeeded() && oversized.failed(),
            "oversized length must fail immediately without waiting for or allocating payload bytes");

    StreamDecoder malformed(Direction::Request);
    const std::vector<std::uint8_t> invalidJson{1, 0, 0, 0, '{'};
    decoded = malformed.feed(invalidJson);
    require(!decoded.succeeded(), "malformed JSON must fail the byte channel");

    StreamDecoder truncated(Direction::Request);
    require(truncated.feed(std::span(header).first(2)).succeeded(),
            "a partial header is allowed until EOF is observed");
    decoded = truncated.finish();
    require(!decoded.succeeded() && decoded.error.find("truncated") != std::string::npos,
            "EOF in a partial frame must be reported as truncation");

    std::string error;
    auto tooLarge = request(1, {{"plan", std::string(MaximumRequestBytes, 'x')}});
    require(encode_frame(tooLarge, Direction::Request, error).empty() && !error.empty(),
            "a serialized request plan must stay within the 64 KiB JSON-byte limit");
    auto largeEvent = event(1, {{"padding", std::string(MaximumEventBytes, 'x')}});
    require(encode_frame(largeEvent, Direction::Event, error).empty() && !error.empty(),
            "event encoding must enforce the 256 KiB JSON-byte limit");
    auto badEvent = event(1);
    badEvent.runtimeSerial = 0;
    require(encode_frame(badEvent, Direction::Event, error).empty(),
            "events without a runtime serial must not be encoded");
}

void test_capability_validation()
{
    const nlohmann::json catalog = {
        {"schema_version", 1}, {"behaviors", nlohmann::json::array()},
        {"editor_capabilities", {
            {"session_protocol", "platformator-editor-session-v1"},
            {"play_control", true}, {"runtime_inspection", true},
            {"rgba_frame_stream", {{"version", 1}, {"width", 640},
                                    {"height", 360}, {"max_hz", 15}}},
            {"live_asset_reload", false}, {"input_forwarding", false}}}};
    const auto valid = read_session_capabilities(catalog);
    require(valid.supported && valid.playControl && valid.runtimeInspection &&
                valid.frameStream && !valid.liveAssetReload && !valid.inputForwarding,
            "a bounded v1 capability record must be accepted");

    const auto absent = read_session_capabilities({{"schema_version", 1}, {"behaviors", nlohmann::json::array()}});
    require(!absent.supported && absent.error.empty(),
            "catalogs without session capabilities must remain supported as external-run targets");
    auto forwarded = catalog;
    forwarded["editor_capabilities"]["input_forwarding"] = true;
    require(read_session_capabilities(forwarded).supported &&
                read_session_capabilities(forwarded).inputForwarding,
            "input forwarding capability must be accepted");
    auto sixtyHz = catalog;
    sixtyHz["editor_capabilities"]["rgba_frame_stream"]["max_hz"] = 60;
    require(read_session_capabilities(sixtyHz).supported,
            "frame capability max_hz up to 60 must be accepted");
    auto tooFast = catalog;
    tooFast["editor_capabilities"]["rgba_frame_stream"]["max_hz"] = 120;
    require(!read_session_capabilities(tooFast).supported,
            "frame capability max_hz exceeding 60 must be rejected");
    auto wrongSize = catalog;
    wrongSize["editor_capabilities"]["rgba_frame_stream"]["width"] = 1280;
    require(!read_session_capabilities(wrongSize).supported,
            "frame capability dimensions must match the fixed 640 by 360 transport");
    require(!read_session_capabilities({{"schema_version", 2}, {"behaviors", nlohmann::json::array()}}).error.empty(),
            "the existing catalog schema version remains authoritative");
}

void test_bounded_queues_and_gates()
{
    BoundedQueue requests(Direction::Request);
    for (std::uint64_t sequence = 1; sequence <= MaximumQueuedMessages; ++sequence)
        require(requests.enqueue(request(sequence)), "request queue should admit its first 64 messages");
    require(!requests.enqueue(request(MaximumQueuedMessages + 1)) &&
                requests.size() == MaximumQueuedMessages,
            "request queue must reject message 65 before acceptance");
    std::vector<std::uint8_t> frame;
    require(requests.pop(frame) && requests.size() == MaximumQueuedMessages - 1,
            "dequeue must release both a slot and its byte accounting");

    BoundedQueue byteLimitedRequests(Direction::Request);
    const auto largeRequestBody = std::string(16U * 1024U, 'r');
    std::uint64_t requestSequence = 1;
    while (byteLimitedRequests.enqueue(request(requestSequence++, {{"detail", largeRequestBody}})))
    {
    }
    require(byteLimitedRequests.size() < MaximumQueuedMessages &&
                byteLimitedRequests.bytes() <= MaximumQueuedBytes,
            "request queue must enforce its 1 MiB byte cap independently of the 64-message cap");

    BoundedQueue events(Direction::Event);
    const auto largeOptional = std::string(16U * 1024U, 'x');
    std::uint64_t sequence = 1;
    while (events.enqueue(event(sequence++, {{"detail", largeOptional}})))
    {
    }
    require(events.size() < MaximumQueuedMessages &&
                events.bytes() <= MaximumQueuedBytes - ReliableEventReserveBytes,
            "optional events may not consume the reliable control reserve");
    require(events.enqueue(event(sequence, {{"state", "stopped"}}), true) &&
                events.bytes() <= MaximumQueuedBytes,
            "a small reliable state event may use reserved event capacity");

    SequenceTracker sequenceTracker;
    require(sequenceTracker.accept(1) && sequenceTracker.accept(3) &&
                !sequenceTracker.accept(3) && !sequenceTracker.accept(2),
            "message sequences must increase strictly and cannot be replayed");
    InspectionPageGate gate;
    require(gate.try_begin() && gate.in_flight() && !gate.try_begin(),
            "only one inspection page response may be in flight");
    gate.complete();
    require(!gate.in_flight() && gate.try_begin(),
            "a page completion releases the inspection response gate");
}
} // namespace

int main()
{
    try
    {
        test_partial_and_coalesced_frames();
        test_rejects_bad_and_truncated_frames();
        test_capability_validation();
        test_bounded_queues_and_gates();
        std::cout << "editor protocol tests passed\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception &error)
    {
        std::cerr << "editor protocol tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
