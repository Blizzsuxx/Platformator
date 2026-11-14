#include "playsession.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <exception>
#include <limits>
#include <random>
#include <sstream>
#include <string_view>

namespace platformator_editor
{
namespace
{
using namespace platformator::editor_session;
constexpr std::size_t ReservedControlMessages = 8;
constexpr std::size_t ReservedControlBytes = 16U * 1024U;
constexpr std::size_t FrameSlotPayloadBytes = 640U * 360U * 4U;
constexpr std::size_t FrameSlotStride = 640U * 4U;

bool valid_document_identity(const PlaySessionLaunchIdentity &identity) noexcept
{
    return static_cast<bool>(identity.document.projectGeneration) &&
           static_cast<bool>(identity.document.tabId) &&
           static_cast<bool>(identity.document.documentGeneration) &&
           !identity.savedFileIdentity.empty() &&
           !identity.pipeline.canonicalProjectPath.empty() && !identity.pipeline.target.empty() &&
           !identity.pipeline.sourceInputIdentity.empty();
}

bool same_generation(const Message &message, PlayGeneration generation,
                     const std::string &nonce) noexcept
{
    return message.nonce == nonce && message.playGeneration == generation.value;
}

bool known_event(std::string_view type) noexcept
{
    return type == "ready" || type == "state" || type == "step_completed" ||
           type == "frame_ready" || type == "object_page" ||
           type == "object_snapshot" || type == "reload_result" ||
           type == "inspection_busy" || type == "frame_released" ||
           type == "capabilities" || type == "error" ||
           type == "stopped";
}
} // namespace

PlaySession::PlaySession() = default;

bool PlaySession::begin_configuring(PlaySessionLaunchIdentity identity, std::string nonce)
{
    if (state_ != PlaySessionState::Idle && state_ != PlaySessionState::Failed)
    {
        lastError_ = "a play session is already active";
        return false;
    }
    if (!valid_document_identity(identity))
    {
        lastError_ = "play launch identity is incomplete or unsaved";
        return false;
    }
    if (nonce.empty())
        nonce = make_nonce();
    if (nonce.size() < 16 || nonce.size() > 128)
    {
        lastError_ = "play-session nonce must contain 16 to 128 bytes";
        return false;
    }

    try
    {
        generation_ = generations_.next();
    }
    catch (const std::exception &error)
    {
        lastError_ = error.what();
        return false;
    }
    reset_protocol_state();
    stdoutLog_.clear();
    stderrLog_.clear();
    processStarted_ = false;
    childReaped_ = true;
    processStopRequested_ = false;
    stopDeadline_ = {};
    startupDeadline_ = {};
    launchIdentity_ = std::move(identity);
    nonce_ = std::move(nonce);
    runtimeSerial_ = {};
    capabilities_ = {};
    lastError_.clear();
    state_ = PlaySessionState::Configuring;
    return true;
}

bool PlaySession::begin_building()
{
    if (state_ != PlaySessionState::Configuring)
        return false;
    state_ = PlaySessionState::Building;
    return true;
}

bool PlaySession::finalize_pipeline_identity(PipelineIdentity identity)
{
    if (state_ != PlaySessionState::Building || !launchIdentity_ ||
        identity.canonicalProjectPath != launchIdentity_->pipeline.canonicalProjectPath ||
        identity.profile != launchIdentity_->pipeline.profile ||
        identity.target != launchIdentity_->pipeline.target ||
        identity.sourceInputIdentity != launchIdentity_->pipeline.sourceInputIdentity ||
        identity.executablePath.empty() || identity.binaryIdentity.empty())
    {
        lastError_ = "finalized pipeline identity is incomplete or changed its selected inputs";
        return false;
    }
    launchIdentity_->pipeline = std::move(identity);
    return true;
}

bool PlaySession::begin_starting(std::unique_ptr<SessionChannelPair> channels,
                                 SessionCapabilities initialCapabilities)
{
    if (state_ != PlaySessionState::Building && state_ != PlaySessionState::Configuring)
    {
        lastError_ = "build must complete before session startup";
        return false;
    }
    if (!channels || !channels->commands || !channels->events)
    {
        lastError_ = "session startup requires separate command and event channels";
        return false;
    }
    if (channels->frameMapping && channels->frameMapping->size() != session_frame_mapping_limit)
    {
        lastError_ = "embedded session requires the fixed two-slot frame mapping size";
        return false;
    }
    capabilities_ = initialCapabilities;
    childCapabilitiesValidated_ = false;
    channels_ = std::move(channels);
    state_ = PlaySessionState::Starting;
    startupDeadline_ = std::chrono::steady_clock::now() + StartupHandshakeTimeout;
    return true;
}

bool PlaySession::start_process(ProcessRequest request,
                                std::unique_ptr<SessionChannelPair> channels)
{
    if (state_ != PlaySessionState::Building && state_ != PlaySessionState::Configuring)
    {
        lastError_ = "build must complete before session startup";
        return false;
    }
    if (!channels || channels->childEndpoints.size() != 3)
    {
        lastError_ = "session process launch requires three inherited child endpoints";
        state_ = PlaySessionState::Stopping;
        return false;
    }
    if (request.argv.empty() || request.argv.front().empty())
    {
        lastError_ = "session process launch requires an executable";
        state_ = PlaySessionState::Stopping;
        return false;
    }
    if (request.sessionChannels != nullptr)
    {
        lastError_ = "session process request already has a channel pair";
        state_ = PlaySessionState::Stopping;
        return false;
    }

    for (const auto &endpoint : channels->childEndpoints)
    {
        if (endpoint.argument.empty() || endpoint.opaqueValue.empty())
        {
            lastError_ = "session child endpoint is missing its launch argument";
            state_ = PlaySessionState::Stopping;
            return false;
        }
    }
    try
    {
        request.argv.push_back("--editor-session-v1");
        for (const auto &endpoint : channels->childEndpoints)
        {
            request.argv.push_back(endpoint.argument);
            request.argv.push_back(endpoint.opaqueValue);
        }
    }
    catch (const std::exception &error)
    {
        lastError_ = error.what();
        state_ = PlaySessionState::Stopping;
        return false;
    }

    if (!begin_starting(std::move(channels)))
    {
        state_ = PlaySessionState::Stopping;
        childReaped_ = true;
        return false;
    }
    if (!queue_command("hello", Json::object()))
    {
        state_ = PlaySessionState::Stopping;
        childReaped_ = true;
        return false;
    }
    request.outputMode = ProcessOutputMode::Separate;
    request.sessionChannels = channels_.get();
    const auto generation = generation_.value;
    const bool launched = process_.start(
        std::move(request), generation,
        [this](ProcessOutput output) {
            if (output.stream == ProcessStream::Stderr)
                stderrLog_.append(output.bytes);
            else
                stdoutLog_.append(output.bytes);
        },
        [this, generation](ProcessCompletion completion) {
            if (completion.generation != generation || generation_.value != generation)
                return;
            processStarted_ = true;
            childReaped_ = true;
            if (!completion.launched)
            {
                if (lastError_.empty())
                    lastError_ = completion.error.empty()
                        ? "session child failed to launch"
                        : std::move(completion.error);
                state_ = PlaySessionState::Stopping;
            }
            else if (state_ != PlaySessionState::Stopping)
            {
                std::string message = "session child exited before Stop";
                if (!completion.error.empty())
                    message += ": " + completion.error;
                else if (completion.terminatingSignal != 0)
                    message += " (signal " + std::to_string(completion.terminatingSignal) + ")";
                else
                    message += " (exit " + std::to_string(completion.exitCode) + ")";
                fail(std::move(message));
            }
        });
    if (!launched)
    {
        if (lastError_.empty())
            lastError_ = "session child could not be started";
        state_ = PlaySessionState::Stopping;
        processStarted_ = false;
        childReaped_ = true;
        return false;
    }
    processStarted_ = true;
    childReaped_ = false;
    return true;
}

void PlaySession::poll_process()
{
    if (!processStarted_)
    {
        if (state_ == PlaySessionState::Starting && startupDeadline_ !=
                std::chrono::steady_clock::time_point{} &&
            std::chrono::steady_clock::now() >= startupDeadline_)
            fail("embedded Game startup handshake timed out; inspect the Console logs or switch Launch Mode to External Game Window");
        return;
    }
    process_.poll();
    if (!process_.running() && !childReaped_)
        childReaped_ = true;

    if (state_ == PlaySessionState::Stopping)
    {
        if (lastError_.empty())
        {
            flush_commands();
            poll_events();
        }
        if (process_.running() && !processStopRequested_ &&
            std::chrono::steady_clock::now() >= stopDeadline_)
        {
            process_.stop();
            processStopRequested_ = true;
        }
    }
    else if (state_ == PlaySessionState::Starting || state_ == PlaySessionState::Running ||
             state_ == PlaySessionState::Paused)
    {
        flush_commands();
        poll_events();
        if (state_ == PlaySessionState::Starting && startupDeadline_ !=
                std::chrono::steady_clock::time_point{} &&
            std::chrono::steady_clock::now() >= startupDeadline_)
            fail("embedded Game startup handshake timed out; inspect the Console logs or switch Launch Mode to External Game Window");
    }
}

bool PlaySession::mark_running(RuntimeSerial runtimeSerial)
{
    if (state_ != PlaySessionState::Starting || !runtimeSerial)
        return false;
    runtimeSerial_ = runtimeSerial;
    state_ = PlaySessionState::Running;
    return true;
}

bool PlaySession::request_pause()
{
    if (state_ != PlaySessionState::Running || pendingPauseSequence_ || pendingResumeSequence_ ||
        !capabilities_.playControl)
        return false;
    std::optional<std::uint64_t> sequence;
    if (!queue_command("pause", Json::object(), &sequence))
        return false;
    pendingPauseSequence_ = sequence;
    return true;
}

bool PlaySession::request_resume()
{
    if (state_ != PlaySessionState::Paused || pendingPauseSequence_ || pendingResumeSequence_ ||
        pendingStepSequence_ || !capabilities_.playControl)
        return false;
    std::optional<std::uint64_t> sequence;
    if (!queue_command("resume", Json::object(), &sequence))
        return false;
    pendingResumeSequence_ = sequence;
    return true;
}

bool PlaySession::request_step()
{
    if (state_ != PlaySessionState::Paused || pendingPauseSequence_ || pendingResumeSequence_ ||
        pendingStepSequence_ || !capabilities_.playControl)
        return false;
    std::optional<std::uint64_t> sequence;
    if (!queue_command("step", Json::object(), &sequence))
        return false;
    pendingStepSequence_ = sequence;
    return true;
}

bool PlaySession::request_capture(bool visible)
{
    if ((state_ != PlaySessionState::Running && state_ != PlaySessionState::Paused) ||
        !capabilities_.frameStream)
        return false;
    if (outgoing_.size() >= MaximumQueuedMessages - ReservedControlMessages ||
        outgoing_.bytes() >= MaximumQueuedBytes - ReservedControlBytes)
    {
        lastError_ = "optional capture request deferred to preserve reliable control queue capacity";
        return false;
    }
    return queue_command("set_capture", Json{{"visible", visible}});
}

bool PlaySession::request_object_page(std::optional<std::uint64_t> cursor,
                                      std::optional<std::uint64_t> snapshotRevision)
{
    if ((state_ != PlaySessionState::Running && state_ != PlaySessionState::Paused) ||
        !capabilities_.runtimeInspection || inspectionRequestPending_ ||
        cursor.has_value() != snapshotRevision.has_value() ||
        (cursor && (!*cursor || !*snapshotRevision)))
        return false;
    Json body{{"runtime_serial", runtimeSerial_.value}, {"page_size", 64}};
    if (cursor)
    {
        body["cursor"] = *cursor;
        body["snapshot_revision"] = *snapshotRevision;
    }
    if (!queue_command("list_objects", std::move(body)))
        return false;
    inspectionRequestPending_ = true;
    return true;
}

bool PlaySession::request_object_snapshot(const Json &handle)
{
    if ((state_ != PlaySessionState::Running && state_ != PlaySessionState::Paused) ||
        !capabilities_.runtimeInspection || inspectionRequestPending_ || !handle.is_object())
        return false;
    const auto runtime = handle.find("runtime_serial");
    const auto slot = handle.find("slot");
    const auto generation = handle.find("generation");
    const auto kind = handle.find("kind");
    if (runtime == handle.end() || !runtime->is_number_unsigned() ||
        runtime->get<std::uint64_t>() != runtimeSerial_.value ||
        slot == handle.end() || !slot->is_number_unsigned() ||
        generation == handle.end() || !generation->is_number_unsigned() ||
        generation->get<std::uint64_t>() == 0 || kind == handle.end() || !kind->is_string())
        return false;
    if (!queue_command("inspect_object", Json{{"runtime_serial", runtimeSerial_.value},
                                                {"handle", handle}}))
        return false;
    inspectionRequestPending_ = true;
    return true;
}

bool PlaySession::copy_frame_slot(std::uint32_t slot, std::uint64_t frameSequence,
                                  std::span<std::byte> destination) noexcept
{
    if (slot >= frameSlots_.size() || frameSequence == 0 || frameSlots_[slot] != frameSequence ||
        frameReleasing_[slot] ||
        !channels_ || !channels_->frameMapping ||
        channels_->frameMapping->size() != session_frame_mapping_limit ||
        destination.size() != FrameSlotPayloadBytes)
        return false;
    const auto source = channels_->frameMapping->bytes();
    const std::size_t offset = static_cast<std::size_t>(slot) * FrameSlotPayloadBytes;
    if (source.size() != session_frame_mapping_limit ||
        offset > source.size() || FrameSlotPayloadBytes > source.size() - offset)
        return false;
    std::atomic_thread_fence(std::memory_order_acquire);
    std::copy_n(source.begin() + static_cast<std::ptrdiff_t>(offset),
                FrameSlotPayloadBytes, destination.begin());
    frameCopied_[slot] = true;
    return true;
}

bool PlaySession::release_frame(std::uint32_t slot, std::uint64_t frameSequence)
{
    if (slot >= frameSlots_.size() || frameSequence == 0 || frameSlots_[slot] != frameSequence ||
        frameReleasing_[slot] || !frameCopied_[slot] || state_ == PlaySessionState::Stopping)
        return false;
    std::optional<std::uint64_t> requestSequence;
    if (!queue_command("release_frame", Json{{"slot", slot},
                                              {"frame_sequence", frameSequence}},
                       &requestSequence))
        return false;
    frameReleasing_[slot] = true;
    frameReleaseRequests_[slot] = *requestSequence;
    return true;
}

bool PlaySession::send_input(const EmbeddedInputEvent &event)
{
    if (state_ != PlaySessionState::Running && state_ != PlaySessionState::Paused)
        return false;
    if (!capabilities_.inputForwarding)
        return false;
    switch (event.kind)
    {
    case EmbeddedInputKind::KeyDown:
        return queue_command("key_down", Json{
            {"scancode", event.scancode},
            {"keycode", event.keycode},
            {"modifiers", event.modifiers}
        });
    case EmbeddedInputKind::KeyUp:
        return queue_command("key_up", Json{
            {"scancode", event.scancode},
            {"keycode", event.keycode},
            {"modifiers", event.modifiers}
        });
    case EmbeddedInputKind::MouseMove:
        return queue_command("mouse_move", Json{
            {"x", event.logicalX},
            {"y", event.logicalY}
        });
    case EmbeddedInputKind::MouseButtonDown:
        return queue_command("mouse_button", Json{
            {"button", event.mouseButton},
            {"down", true},
            {"x", event.logicalX},
            {"y", event.logicalY}
        });
    case EmbeddedInputKind::MouseButtonUp:
        return queue_command("mouse_button", Json{
            {"button", event.mouseButton},
            {"down", false},
            {"x", event.logicalX},
            {"y", event.logicalY}
        });
    case EmbeddedInputKind::ReleaseAll:
        return release_all_inputs();
    }
    return false;
}

bool PlaySession::release_all_inputs()
{
    if (state_ != PlaySessionState::Running && state_ != PlaySessionState::Paused)
        return false;
    if (!capabilities_.inputForwarding)
        return false;
    return queue_command("release_all_inputs", Json::object());
}

bool PlaySession::request_stop()
{
    if (state_ == PlaySessionState::Idle || state_ == PlaySessionState::Failed)
        return false;
    if (state_ == PlaySessionState::Stopping)
        return true;
    const bool childStarted = state_ == PlaySessionState::Starting ||
                              state_ == PlaySessionState::Running ||
                              state_ == PlaySessionState::Paused;
    if (channels_ && childStarted)
    {
        if (!queue_command("stop", Json::object()))
            return false;
    }
    state_ = PlaySessionState::Stopping;
    if (processStarted_ && process_.running())
    {
        processStopRequested_ = false;
        stopDeadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    }
    else
        childReaped_ = true;
    return true;
}

bool PlaySession::finish_stop_after_reap()
{
    if (state_ != PlaySessionState::Stopping && state_ != PlaySessionState::Failed)
        return false;
    if (processStarted_ && !childReaped_)
        return false;
    if (channels_)
    {
        if (channels_->commands)
            channels_->commands->cancel();
        if (channels_->events)
            channels_->events->cancel();
        if (channels_->frameMapping)
            channels_->frameMapping->close();
        channels_.reset();
    }
    const bool failed = !lastError_.empty();
    launchIdentity_.reset();
    capabilities_ = {};
    generation_ = {};
    runtimeSerial_ = {};
    nonce_.clear();
    processStarted_ = false;
    childReaped_ = true;
    processStopRequested_ = false;
    stopDeadline_ = {};
    startupDeadline_ = {};
    reset_protocol_state();
    state_ = failed ? PlaySessionState::Failed : PlaySessionState::Idle;
    return true;
}

bool PlaySession::queue_command(std::string type, Json body,
                                std::optional<std::uint64_t> *acceptedSequence)
{
    if (!channels_ || !channels_->commands || nonce_.empty() || !generation_ ||
        state_ == PlaySessionState::Idle || state_ == PlaySessionState::Stopping ||
        nextRequestSequence_ == 0)
    {
        lastError_ = "session command channel is unavailable";
        return false;
    }
    ProtocolMessage message;
    message.nonce = nonce_;
    message.playGeneration = generation_.value;
    message.sequence = nextRequestSequence_;
    message.type = std::move(type);
    message.body = std::move(body);
    if (!outgoing_.enqueue(message))
    {
        lastError_ = std::string(outgoing_.last_error());
        return false;
    }
    if (acceptedSequence != nullptr)
        *acceptedSequence = message.sequence;
    nextRequestSequence_ = nextRequestSequence_ == std::numeric_limits<std::uint64_t>::max()
        ? 0 : nextRequestSequence_ + 1;
    return true;
}

void PlaySession::flush_commands()
{
    if (!channels_ || !channels_->commands || state_ == PlaySessionState::Idle)
        return;
    std::size_t remaining = MaximumPumpBytesPerCall;
    while (remaining > 0)
    {
        const auto frame = outgoing_.front();
        if (frame.empty())
            return;
        const auto count = std::min(remaining, frame.size());
        const auto result = channels_->commands->write(std::as_bytes(frame.first(count)));
        if (result.status == SessionChannelStatus::WouldBlock ||
            (result.status == SessionChannelStatus::Progress && result.bytesTransferred == 0))
            return;
        if (result.status == SessionChannelStatus::Progress)
        {
            if (result.bytesTransferred > count || !outgoing_.consume(result.bytesTransferred))
            {
                fail("session command channel reported an invalid partial-write length");
                return;
            }
            remaining -= result.bytesTransferred;
            continue;
        }
        if (result.status == SessionChannelStatus::Closed && state_ == PlaySessionState::Stopping)
            return;
        fail(result.error.empty() ? "session command channel closed unexpectedly" : result.error);
        return;
    }
}

void PlaySession::poll_events()
{
    if (!channels_ || !channels_->events || state_ == PlaySessionState::Idle)
        return;
    constexpr std::size_t readChunkBytes = 16U * 1024U;
    std::array<std::byte, readChunkBytes> buffer{};
    std::size_t remaining = MaximumPumpBytesPerCall;
    std::size_t acceptedEvents = 0;
    while (acceptedEvents < MaximumEventsPerCall)
    {
        if (!pendingDecodedEvents_.empty())
        {
            ProtocolMessage message = std::move(pendingDecodedEvents_.front());
            pendingDecodedEvents_.pop_front();
            std::string encodedError;
            const auto encoded = encode_frame(message, Direction::Event, encodedError);
            if (!encodedError.empty())
            {
                fail("buffered session event could not be accounted: " + encodedError);
                return;
            }
            pendingDecodedEventBytes_ -= std::min(pendingDecodedEventBytes_, encoded.size());
            ++acceptedEvents;
            if (!handle_event(std::move(message)))
                return;
            continue;
        }
        if (remaining == 0)
            return;
        const auto count = std::min(remaining, buffer.size());
        const auto result = channels_->events->read(std::span<std::byte>(buffer).first(count));
        if (result.status == SessionChannelStatus::WouldBlock ||
            (result.status == SessionChannelStatus::Progress && result.bytesTransferred == 0))
            return;
        if (result.status == SessionChannelStatus::Progress)
        {
            if (result.bytesTransferred > count)
            {
                fail("session event channel reported an invalid read length");
                return;
            }
            remaining -= result.bytesTransferred;
            auto decoded = incoming_.feed(std::span<const std::uint8_t>(
                reinterpret_cast<const std::uint8_t *>(buffer.data()), result.bytesTransferred));
            if (!decoded.succeeded())
            {
                fail(decoded.error);
                return;
            }
            for (auto &message : decoded.messages)
            {
                std::string encodedError;
                const auto encoded = encode_frame(message, Direction::Event, encodedError);
                if (!encodedError.empty() ||
                    encoded.size() > MaximumQueuedBytes -
                        std::min(pendingDecodedEventBytes_, MaximumQueuedBytes))
                {
                    fail(encodedError.empty()
                        ? "decoded session event backlog exceeded its byte bound"
                        : "decoded session event could not be accounted: " + encodedError);
                    return;
                }
                pendingDecodedEventBytes_ += encoded.size();
                pendingDecodedEvents_.push_back(std::move(message));
            }
            continue;
        }
        if (result.status == SessionChannelStatus::Closed && state_ == PlaySessionState::Stopping)
            return;
        fail(result.error.empty() ? "session event channel closed unexpectedly" : result.error);
        return;
    }
}

std::optional<PlaySession::ProtocolMessage> PlaySession::take_event()
{
    if (events_.empty())
        return std::nullopt;
    ProtocolMessage message = std::move(events_.front());
    events_.pop_front();
    std::string error;
    const auto frame = platformator::editor_session::encode_frame(
        message, platformator::editor_session::Direction::Event, error);
    if (error.empty())
        eventBytes_ -= std::min(eventBytes_, frame.size());
    return message;
}

bool PlaySession::handle_event(ProtocolMessage message)
{
    using namespace platformator::editor_session;
    if (!same_generation(message, generation_, nonce_))
    {
        fail("session event belongs to a stale nonce or play generation");
        return false;
    }
    if (!eventSequences_.accept(message.sequence))
    {
        fail("session event sequence was replayed or moved backwards");
        return false;
    }
    if (!known_event(message.type))
    {
        fail("session emitted an unknown event type: " + message.type);
        return false;
    }

    if (message.type == "ready")
    {
        if (state_ != PlaySessionState::Starting || !childCapabilitiesValidated_)
        {
            fail("ready event arrived before a valid child capabilities handshake");
            return false;
        }
        if (startupRuntimeSerial_ != 0 && startupRuntimeSerial_ != message.runtimeSerial)
        {
            fail("ready event belongs to a different runtime than the capability handshake");
            return false;
        }
        if (!mark_running(RuntimeSerial{message.runtimeSerial}))
        {
            fail("ready event arrived outside Starting or had an invalid runtime serial");
            return false;
        }
    }
    else if (message.type == "state")
    {
        if (!runtimeSerial_ || message.runtimeSerial != runtimeSerial_.value)
        {
            fail("state event belongs to a stale runtime serial");
            return false;
        }
        const auto stateField = message.body.find("state");
        const auto requestField = message.body.find("request_sequence");
        if (stateField == message.body.end() || !stateField->is_string() ||
            requestField == message.body.end() || !requestField->is_number_unsigned())
        {
            fail("state acknowledgement is missing its state or request sequence");
            return false;
        }
        const auto requestSequence = requestField->get<std::uint64_t>();
        const auto reportedState = stateField->get<std::string>();
        if (reportedState == "paused" && pendingPauseSequence_ == requestSequence)
        {
            pendingPauseSequence_.reset();
            state_ = PlaySessionState::Paused;
        }
        else if (reportedState == "running" && pendingResumeSequence_ == requestSequence)
        {
            pendingResumeSequence_.reset();
            state_ = PlaySessionState::Running;
        }
        else
        {
            fail("state acknowledgement does not match an accepted pause or resume request");
            return false;
        }
    }
    else if (message.type == "step_completed")
    {
        if (state_ != PlaySessionState::Paused || !runtimeSerial_ ||
            message.runtimeSerial != runtimeSerial_.value)
        {
            fail("step completion arrived outside the current paused runtime");
            return false;
        }
        const auto requestField = message.body.find("request_sequence");
        const auto stepField = message.body.find("step_number");
        if (requestField == message.body.end() || !requestField->is_number_unsigned() ||
            stepField == message.body.end() || !stepField->is_number_unsigned())
        {
            fail("step completion is missing its request sequence or step number");
            return false;
        }
        const auto requestSequence = requestField->get<std::uint64_t>();
        const auto stepNumber = stepField->get<std::uint64_t>();
        if (!pendingStepSequence_ || requestSequence != *pendingStepSequence_ ||
            stepNumber <= lastStepNumber_ || completedSteps_ == std::numeric_limits<std::uint64_t>::max())
        {
            fail("step completion is duplicated, stale, or out of order");
            return false;
        }
        pendingStepSequence_.reset();
        lastStepNumber_ = stepNumber;
        ++completedSteps_;
    }
    else if (message.type == "stopped")
    {
        if (state_ != PlaySessionState::Stopping || !runtimeSerial_ ||
            message.runtimeSerial != runtimeSerial_.value)
        {
            fail("stopped event arrived for a session that is not stopping");
            return false;
        }
    }
    else if (message.type == "capabilities")
    {
        if (state_ != PlaySessionState::Starting || childCapabilitiesValidated_ || runtimeSerial_)
        {
            fail("capability event was duplicated or arrived after session startup");
            return false;
        }
        if (message.runtimeSerial == 0 ||
            (runtimeSerial_ && message.runtimeSerial != runtimeSerial_.value))
        {
            fail("capability event belongs to an invalid or stale runtime serial");
            return false;
        }
        const Json catalogEnvelope{{"schema_version", 1}, {"behaviors", Json::array()},
                                   {"editor_capabilities", message.body}};
        auto childCapabilities = read_session_capabilities(catalogEnvelope);
        if (!childCapabilities.supported || !childCapabilities.frameStream)
        {
            fail(childCapabilities.error.empty()
                     ? "the built target does not advertise a compatible embedded Game view; choose an external window"
                     : childCapabilities.error + "; choose an external game window");
            return false;
        }
        capabilities_ = std::move(childCapabilities);
        if (!runtimeSerial_)
            startupRuntimeSerial_ = message.runtimeSerial;
        childCapabilitiesValidated_ = true;
    }
    else if (message.type == "frame_ready" && !capabilities_.frameStream)
    {
        fail("target emitted a frame without advertising frame streaming");
        return false;
    }
    else if (message.type == "frame_ready")
    {
        const auto slotField = message.body.find("slot");
        const auto sequenceField = message.body.find("frame_sequence");
        const auto widthField = message.body.find("width");
        const auto heightField = message.body.find("height");
        const auto strideField = message.body.find("stride");
        const auto formatField = message.body.find("format");
        const auto integer_is = [](const auto &field, const auto &end,
                                   std::int64_t expected) {
            return field != end && field->is_number_integer() &&
                   field->template get<std::int64_t>() == expected;
        };
        if (slotField == message.body.end() || !slotField->is_number_unsigned() ||
            sequenceField == message.body.end() || !sequenceField->is_number_unsigned() ||
            !integer_is(widthField, message.body.end(), 640) ||
            !integer_is(heightField, message.body.end(), 360) ||
            !integer_is(strideField, message.body.end(), 2560) ||
            formatField == message.body.end() || !formatField->is_string() ||
            formatField->get<std::string>() != "rgba8")
        {
            fail("frame-ready event has invalid slot or pixel metadata");
            return false;
        }
        const auto slot = slotField->get<std::uint64_t>();
        const auto sequence = sequenceField->get<std::uint64_t>();
        if (slot >= frameSlots_.size() || sequence <= lastFrameSequence_ ||
            frameSlots_[slot] != 0 || !channels_ || !channels_->frameMapping ||
            channels_->frameMapping->size() != session_frame_mapping_limit)
        {
            fail("frame-ready event reuses a slot or belongs to stale frame storage");
            return false;
        }
        frameSlots_[slot] = sequence;
        frameCopied_[slot] = false;
        lastFrameSequence_ = sequence;
    }
    else if (message.type == "frame_released")
    {
        const auto slotField = message.body.find("slot");
        const auto sequenceField = message.body.find("frame_sequence");
        const auto requestField = message.body.find("request_sequence");
        if (slotField == message.body.end() || !slotField->is_number_unsigned() ||
            sequenceField == message.body.end() || !sequenceField->is_number_unsigned() ||
            requestField == message.body.end() || !requestField->is_number_unsigned())
        {
            fail("frame-release acknowledgement is missing its slot or sequence");
            return false;
        }
        const auto slot = slotField->get<std::uint64_t>();
        if (slot >= frameSlots_.size() || !frameReleasing_[slot] ||
            frameSlots_[slot] != sequenceField->get<std::uint64_t>() ||
            frameReleaseRequests_[slot] != requestField->get<std::uint64_t>())
        {
            fail("frame-release acknowledgement is stale or does not match its accepted request");
            return false;
        }
        frameSlots_[slot] = 0;
        frameReleaseRequests_[slot] = 0;
        frameReleasing_[slot] = false;
        frameCopied_[slot] = false;
    }
    else if ((message.type == "object_page" || message.type == "object_snapshot") &&
             !capabilities_.runtimeInspection)
    {
        fail("target emitted inspection data without advertising runtime inspection");
        return false;
    }
    else if (message.type == "reload_result" && !capabilities_.liveAssetReload)
    {
        fail("target emitted a reload result without advertising live asset reload");
        return false;
    }
    else if (message.type == "error")
    {
        const auto errorField = message.body.find("message");
        fail(errorField != message.body.end() && errorField->is_string()
                 ? errorField->get<std::string>() : "child session reported an error");
    }

    if (message.type == "object_page" || message.type == "object_snapshot" ||
        message.type == "inspection_busy")
        inspectionRequestPending_ = false;

    if (message.type != "ready" && message.type != "state" && message.type != "step_completed" &&
        message.type != "stopped" && message.type != "error" &&
        !(message.type == "capabilities" && !runtimeSerial_) &&
        (!runtimeSerial_ || message.runtimeSerial != runtimeSerial_.value))
    {
        fail("session event belongs to a stale runtime serial");
        return false;
    }

    std::string encodeError;
    const auto encoded = encode_frame(message, Direction::Event, encodeError);
    if (!encodeError.empty() || events_.size() >= MaximumQueuedMessages ||
        encoded.size() > MaximumQueuedBytes - std::min(eventBytes_, MaximumQueuedBytes))
    {
        fail("bounded session event inbox is full or could not encode a validated event");
        return false;
    }
    eventBytes_ += encoded.size();
    events_.push_back(std::move(message));
    return true;
}

void PlaySession::fail(std::string error) noexcept
{
    const bool startupFailure = state_ == PlaySessionState::Starting;
    if (startupFailure && error.find("inspect the Console logs") == std::string::npos)
        error += "; inspect the Console logs or switch Launch Mode to External Game Window";
    if (lastError_.empty())
        lastError_ = std::move(error);
    if (state_ != PlaySessionState::Idle && state_ != PlaySessionState::Failed)
    {
        if (processStarted_ && process_.running())
        {
            state_ = PlaySessionState::Stopping;
            if (stopDeadline_ == std::chrono::steady_clock::time_point{})
                stopDeadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        }
        else
            state_ = PlaySessionState::Failed;
    }
}

void PlaySession::reset_protocol_state() noexcept
{
    childCapabilitiesValidated_ = false;
    eventSequences_.reset();
    outgoing_ = platformator::editor_session::BoundedQueue(
        platformator::editor_session::Direction::Request);
    incoming_.reset();
    pendingDecodedEvents_.clear();
    pendingDecodedEventBytes_ = 0;
    events_.clear();
    eventBytes_ = 0;
    pendingPauseSequence_.reset();
    pendingResumeSequence_.reset();
    pendingStepSequence_.reset();
    inspectionRequestPending_ = false;
    startupRuntimeSerial_ = 0;
    frameSlots_.fill(0);
    frameReleaseRequests_.fill(0);
    frameReleasing_.fill(false);
    frameCopied_.fill(false);
    lastFrameSequence_ = 0;
    nextRequestSequence_ = 1;
    completedSteps_ = 0;
    lastStepNumber_ = 0;
}

std::string PlaySession::make_nonce()
{
    static std::atomic<std::uint64_t> fallbackSequence{1};
    std::array<std::uint32_t, 4> words{};
    try
    {
        std::random_device source;
        for (auto &word : words)
            word = source();
    }
    catch (...)
    {
        const auto now = static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        words = {static_cast<std::uint32_t>(now), static_cast<std::uint32_t>(now >> 32U),
                 static_cast<std::uint32_t>(fallbackSequence.fetch_add(1, std::memory_order_relaxed)),
                 static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&fallbackSequence))};
    }
    std::ostringstream output;
    output << std::hex;
    for (const auto word : words)
        output.width(8), output.fill('0'), output << word;
    return output.str();
}

PlaySessionState PlaySession::state() const noexcept { return state_; }
PlayGeneration PlaySession::generation() const noexcept { return generation_; }
RuntimeSerial PlaySession::runtime_serial() const noexcept { return runtimeSerial_; }
const std::string &PlaySession::nonce() const noexcept { return nonce_; }
const std::string &PlaySession::last_error() const noexcept { return lastError_; }
const std::optional<PlaySessionLaunchIdentity> &PlaySession::launch_identity() const noexcept
{
    return launchIdentity_;
}
const platformator::editor_session::SessionCapabilities &PlaySession::capabilities() const noexcept
{
    return capabilities_;
}
std::size_t PlaySession::queued_commands() const noexcept { return outgoing_.size(); }
std::size_t PlaySession::queued_command_bytes() const noexcept { return outgoing_.bytes(); }
std::size_t PlaySession::queued_events() const noexcept { return events_.size(); }
std::uint64_t PlaySession::completed_steps() const noexcept { return completedSteps_; }

bool PlaySession::matches(const PlaySessionLaunchIdentity &identity) const noexcept
{
    return launchIdentity_ && *launchIdentity_ == identity;
}

bool PlaySession::process_running() const noexcept { return process_.running(); }
bool PlaySession::child_reaped() const noexcept { return childReaped_; }
const BoundedConsole &PlaySession::stdout_log() const noexcept { return stdoutLog_; }
const BoundedConsole &PlaySession::stderr_log() const noexcept { return stderrLog_; }
} // namespace platformator_editor
