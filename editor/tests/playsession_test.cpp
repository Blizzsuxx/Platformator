#include "playsession.h"
#include "behaviorcatalog.h"
#include "test_support.h"
#if PLATFORMATOR_ENABLE_EDITOR_SESSION
#include "editorsession.h"
#include "panels/game.h"
#endif

#include <editorprotocol.h>
#include <platformator/runner.h>
#include <platformator/runtime.h>
#include <platformator/scriptregistration.h>
#include "sdlwindow.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cerrno>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if !defined(_WIN32) && PLATFORMATOR_ENABLE_EDITOR_SESSION
#include "platform/sessionchannel_posix.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{
    using namespace platformator::editor_session;
    using namespace platformator_editor;
    using namespace std::chrono_literals;

    class SessionInspectionBehavior final : public Behavior
    {
    public:
        bool enabled = true;
        std::int64_t score = 73;
        std::string label = "runtime inspector";

        SERIALIZABLE_SCRIPT(SessionInspectionBehavior, enabled, score, label);
    };

    class OpaqueSessionInspectionBehavior final : public Behavior
    {
    public:
        std::string getTypeName() const override { return "OpaqueSessionInspectionBehavior"; }
        void serialize(nlohmann::json &) const override {}
        void deserialize(const nlohmann::json &) override {}
        void resolveReferences(platformator::ReferenceResolver &) override {}
    };

    void require(bool condition, const std::string &message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    struct FakePipe
    {
        std::deque<std::byte> readable;
        std::vector<std::byte> written;
        std::size_t maximumRead = 7;
        std::size_t maximumWrite = 5;
        bool blockWrites = false;
        bool closed = false;
        bool cancelled = false;
    };

    class FakeSessionChannel final : public SessionChannel
    {
    public:
        explicit FakeSessionChannel(std::shared_ptr<FakePipe> pipe) : pipe_(std::move(pipe)) {}

        SessionChannelResult read(std::span<std::byte> destination) override
        {
            if (pipe_->cancelled)
                return {SessionChannelStatus::Cancelled, 0, {}};
            if (pipe_->readable.empty())
                return {pipe_->closed ? SessionChannelStatus::Closed : SessionChannelStatus::WouldBlock,
                        0,
                        {}};
            const auto count = std::min({destination.size(), pipe_->readable.size(), pipe_->maximumRead});
            for (std::size_t index = 0; index < count; ++index)
            {
                destination[index] = pipe_->readable.front();
                pipe_->readable.pop_front();
            }
            return {SessionChannelStatus::Progress, count, {}};
        }

        SessionChannelResult write(std::span<const std::byte> source) override
        {
            if (pipe_->cancelled)
                return {SessionChannelStatus::Cancelled, 0, {}};
            if (pipe_->closed)
                return {SessionChannelStatus::Closed, 0, {}};
            if (pipe_->blockWrites)
                return {SessionChannelStatus::WouldBlock, 0, {}};
            if (source.empty())
                return {SessionChannelStatus::Progress, 0, {}};
            const auto count = std::min(source.size(), pipe_->maximumWrite);
            pipe_->written.insert(pipe_->written.end(), source.begin(), source.begin() + static_cast<std::ptrdiff_t>(count));
            return {SessionChannelStatus::Progress, count, {}};
        }

        void cancel() noexcept override { pipe_->cancelled = true; }
        void close() noexcept override { pipe_->closed = true; }

    private:
        std::shared_ptr<FakePipe> pipe_;
    };

    class FakeMapping final : public SessionMapping
    {
    public:
        FakeMapping(std::size_t size, std::shared_ptr<bool> closedState)
            : bytes_(size), closedState_(std::move(closedState)) {}
        std::span<std::byte> bytes() noexcept override { return bytes_; }
        std::size_t size() const noexcept override { return bytes_.size(); }
        void close() noexcept override { *closedState_ = true; }

    private:
        std::vector<std::byte> bytes_;
        std::shared_ptr<bool> closedState_;
    };

    struct FakeTransport
    {
        std::unique_ptr<SessionChannelPair> pair = std::make_unique<SessionChannelPair>();
        std::shared_ptr<FakePipe> commandPipe = std::make_shared<FakePipe>();
        std::shared_ptr<FakePipe> eventPipe = std::make_shared<FakePipe>();
        std::shared_ptr<bool> mappingClosed = std::make_shared<bool>(false);
        FakeMapping *mapping = nullptr;

        explicit FakeTransport(bool withMapping = false)
        {
            pair->commands = std::make_unique<FakeSessionChannel>(commandPipe);
            pair->events = std::make_unique<FakeSessionChannel>(eventPipe);
            if (withMapping)
            {
                auto mappingOwner = std::make_unique<FakeMapping>(session_frame_mapping_limit,
                                                                  mappingClosed);
                mapping = mappingOwner.get();
                pair->frameMapping = std::move(mappingOwner);
            }
        }
    };

    PlaySessionLaunchIdentity launch_identity()
    {
        return {{ProjectGeneration{1}, TabId{2}, DocumentGeneration{3}},
                0,
                "saved-scene:file-identity",
                {"/project", "debug", "game", "/project/bin/game", "binary", "source-inputs"}};
    }

    SessionCapabilities play_capabilities(bool frames = false)
    {
        SessionCapabilities result;
        result.supported = true;
        result.playControl = true;
        result.runtimeInspection = true;
        result.frameStream = frames;
        return result;
    }

    Json child_capabilities_body(bool frames = false)
    {
        Json body{{"session_protocol", "platformator-editor-session-v1"},
                  {"play_control", true},
                  {"runtime_inspection", true},
                  {"input_forwarding", false}};
        if (frames)
            body["rgba_frame_stream"] = Json{{"version", 1}, {"width", 640}, {"height", 360}, {"max_hz", 15}};
        return body;
    }

    void emit_event(FakePipe &pipe, const std::string &nonce, std::uint64_t generation,
                    std::uint64_t sequence, std::uint64_t runtimeSerial,
                    std::string type, Json body = Json::object())
    {
        Message message;
        message.nonce = nonce;
        message.playGeneration = generation;
        message.sequence = sequence;
        message.runtimeSerial = runtimeSerial;
        message.type = std::move(type);
        message.body = std::move(body);
        std::string error;
        const auto frame = encode_frame(message, Direction::Event, error);
        require(error.empty(), "test event must encode: " + error);
        for (const auto byte : frame)
            pipe.readable.push_back(static_cast<std::byte>(byte));
    }

    std::vector<Message> decode_requests(const FakePipe &pipe)
    {
        std::vector<std::uint8_t> bytes;
        bytes.reserve(pipe.written.size());
        for (const auto byte : pipe.written)
            bytes.push_back(std::to_integer<std::uint8_t>(byte));
        StreamDecoder decoder(Direction::Request);
        auto decoded = decoder.feed(bytes);
        require(decoded.succeeded(), "written command bytes must form valid requests: " + decoded.error);
        require(decoder.finish().succeeded(), "written command stream must not be truncated");
        return decoded.messages;
    }

    void test_runner_catalog_is_runtime_only_and_option_gated()
    {
        require(platformator::Runtime::tryGetCurrent() == nullptr && SDL_WasInit(0) == 0,
                "play-session capability query must begin without Runtime or SDL initialization");

        std::ostringstream output;
        std::ostringstream errors;
        auto *previousOutput = std::cout.rdbuf(output.rdbuf());
        auto *previousErrors = std::cerr.rdbuf(errors.rdbuf());
        char executable[] = "playsession-fixture";
        char query[] = "--editor-catalog";
        char *arguments[] = {executable, query};
        const int status = platformator::run(2, arguments, "");
        std::cout.rdbuf(previousOutput);
        std::cerr.rdbuf(previousErrors);

        require(status == 0 && errors.str().empty(),
                "the stock runner must answer its exclusive catalog query without stderr output");
        const auto catalog = nlohmann::json::parse(output.str());
        require(catalog.is_object() && catalog.value("schema_version", 0) == 1 &&
                    catalog.contains("behaviors") && catalog["behaviors"].is_array(),
                "session negotiation must preserve the schema-v1 behavior catalog envelope");
#if defined(PLATFORMATOR_ENABLE_EDITOR_SESSION) && PLATFORMATOR_ENABLE_EDITOR_SESSION
        const auto capabilities = platformator::editor_session::read_session_capabilities(catalog);
        require(capabilities.supported && capabilities.playControl && capabilities.runtimeInspection &&
                    capabilities.inputForwarding,
                "an opt-in stock runner advertises owner-thread controls and input forwarding");
#else
        require(!catalog.contains("editor_capabilities"),
                "a default-off stock runner must not advertise editor session capabilities");
#endif
        require(platformator::Runtime::tryGetCurrent() == nullptr && SDL_WasInit(0) == 0,
                "catalog capability negotiation must not initialize Runtime or SDL");
    }

    void advance_to_starting(PlaySession &session, FakeTransport &transport,
                             SessionCapabilities capabilities = play_capabilities())
    {
        require(session.begin_configuring(launch_identity(), "0123456789abcdef"),
                "a complete saved launch identity and private nonce must configure a session");
        require(session.begin_building() && session.begin_catalog_check(),
                "session setup must follow Configuring → Building → CatalogCheck");
        require(session.begin_starting(std::move(capabilities), std::move(transport.pair)),
                "a compatible capability set and separate channels must enter Starting");
    }

    void test_built_pipeline_identity_is_finalized_before_catalog_validation()
    {
        PlaySession session;
        auto identity = launch_identity();
        require(session.begin_configuring(identity, "0123456789abcdef") && session.begin_building(),
                "a launch session enters Building before the output executable is identified");
        auto builtIdentity = identity.pipeline;
        builtIdentity.executablePath = "/project/build/game";
        builtIdentity.binaryIdentity = "1234:5678";
        require(session.finalize_pipeline_identity(builtIdentity) &&
                    session.launch_identity()->pipeline == builtIdentity &&
                    session.begin_catalog_check(),
                "the built executable identity is finalized before catalog validation begins");

        PlaySession rejected;
        require(rejected.begin_configuring(identity, "0123456789abcdef") && rejected.begin_building(),
                "a second launch session enters Building for the invalidation check");
        builtIdentity = identity.pipeline;
        builtIdentity.canonicalProjectPath = "/different-project";
        builtIdentity.binaryIdentity = "1234:5678";
        require(!rejected.finalize_pipeline_identity(builtIdentity) &&
                    rejected.launch_identity()->pipeline == identity.pipeline,
                "build completion cannot replace the selected project identity");
    }

    void test_partial_nonblocking_pause_step_resume_and_stop()
    {
        PlaySession session;
        FakeTransport transport;
        auto *mapping = transport.mapping;
        advance_to_starting(session, transport);
        const auto generation = session.generation();
        require(session.matches(launch_identity()) && session.state() == PlaySessionState::Starting,
                "the active play session must retain its immutable launch identity");
        require(session.mark_running(RuntimeSerial{44}), "the child ready event establishes one runtime serial");

        transport.commandPipe->blockWrites = true;
        require(session.request_pause(), "pause is accepted once while Running");
        session.flush_commands();
        require(session.queued_commands() == 1 && transport.commandPipe->written.empty(),
                "a full nonblocking channel retains the accepted command without blocking or losing it");
        transport.commandPipe->blockWrites = false;
        session.flush_commands();
        auto requests = decode_requests(*transport.commandPipe);
        require(requests.size() == 1 && requests[0].type == "pause" && requests[0].sequence == 1 &&
                    session.queued_commands() == 0,
                "partial command writes must flush one complete sequenced request");

        emit_event(*transport.eventPipe, session.nonce(), generation.value, 1, 44, "state",
                   Json{{"state", "paused"}, {"request_sequence", 1}});
        session.poll_events();
        require(session.state() == PlaySessionState::Paused && session.queued_events() == 1,
                "pause takes effect only after its matching child acknowledgement");
        const auto pauseAck = session.take_event();
        require(pauseAck && pauseAck->type == "state" && session.queued_events() == 0,
                "validated acknowledgements are available through a bounded event inbox");

        require(session.request_step(), "one Step is accepted while Paused");
        require(!session.request_step(), "a second Step cannot be accepted while one is pending");
        session.flush_commands();
        requests = decode_requests(*transport.commandPipe);
        require(requests.size() == 2 && requests.back().type == "step" && requests.back().sequence == 2,
                "step command sequences remain monotonic after pause");
        emit_event(*transport.eventPipe, session.nonce(), generation.value, 2, 44, "step_completed",
                   Json{{"request_sequence", 2}, {"step_number", 1}});
        session.poll_events();
        require(session.state() == PlaySessionState::Paused && session.completed_steps() == 1,
                "an accepted Step completes exactly once and leaves the session Paused");
        (void)session.take_event();

        require(session.request_resume(), "Resume is accepted after the Step completion");
        session.flush_commands();
        requests = decode_requests(*transport.commandPipe);
        require(requests.size() == 3 && requests.back().type == "resume" && requests.back().sequence == 3,
                "resume uses the next monotonic request sequence");
        emit_event(*transport.eventPipe, session.nonce(), generation.value, 3, 44, "state",
                   Json{{"state", "running"}, {"request_sequence", 3}});
        session.poll_events();
        require(session.state() == PlaySessionState::Running,
                "Resume takes effect only after its matching running acknowledgement");
        (void)session.take_event();

        require(session.request_stop() && session.state() == PlaySessionState::Stopping,
                "stop moves a live session to Stopping");
        session.flush_commands();
        requests = decode_requests(*transport.commandPipe);
        require(requests.size() == 4 && requests.back().type == "stop",
                "stop is sent over the owned command channel");
        require(session.finish_stop_after_reap() && session.state() == PlaySessionState::Idle &&
                    !session.launch_identity() && !session.generation(),
                "session identity is invalidated only after the child has been reaped");
        require(transport.commandPipe->cancelled && transport.eventPipe->cancelled,
                "session channels are cancelled during post-reap cleanup");
        (void)mapping;
    }

    void test_stale_and_replayed_events_fail_closed()
    {
        PlaySession session;
        FakeTransport transport;
        advance_to_starting(session, transport);
        const auto generation = session.generation();
        emit_event(*transport.eventPipe, "fedcba9876543210", generation.value, 1, 44, "ready");
        session.poll_events();
        require(session.state() == PlaySessionState::Stopping &&
                    session.last_error().find("stale nonce") != std::string::npos &&
                    !transport.commandPipe->cancelled && !transport.eventPipe->cancelled,
                "foreign session events enter Stopping without cancelling I/O before child reap");
        require(session.finish_stop_after_reap() && transport.commandPipe->cancelled &&
                    transport.eventPipe->cancelled,
                "failed session I/O is cancelled during owner cleanup after reap");

        PlaySession replaySession;
        FakeTransport replayTransport;
        advance_to_starting(replaySession, replayTransport);
        const auto replayGeneration = replaySession.generation();
        emit_event(*replayTransport.eventPipe, replaySession.nonce(), replayGeneration.value, 1, 44, "ready");
        replaySession.poll_events();
        require(replaySession.state() == PlaySessionState::Running,
                "a matching Ready event starts a session exactly once");
        (void)replaySession.take_event();
        emit_event(*replayTransport.eventPipe, replaySession.nonce(), replayGeneration.value, 1, 44, "state",
                   Json{{"state", "paused"}, {"request_sequence", 1}});
        replaySession.poll_events();
        require(replaySession.state() == PlaySessionState::Stopping &&
                    replaySession.last_error().find("replayed") != std::string::npos &&
                    !replayTransport.commandPipe->cancelled && !replayTransport.eventPipe->cancelled,
                "replayed event sequences fail closed before changing session state");
        require(replaySession.finish_stop_after_reap() && replayTransport.commandPipe->cancelled &&
                    replayTransport.eventPipe->cancelled,
                "replayed sessions cancel transport during owner cleanup after reap");
    }

    void test_capability_and_queue_bounds()
    {
        PlaySession unsupported;
        FakeTransport unsupportedTransport;
        require(unsupported.begin_configuring(launch_identity(), "0123456789abcdef") &&
                    unsupported.begin_building() && unsupported.begin_catalog_check(),
                "unsupported capability fixture reaches catalog negotiation");
        auto noControl = play_capabilities();
        noControl.playControl = false;
        require(unsupported.begin_starting(noControl, std::move(unsupportedTransport.pair)) &&
                    unsupported.mark_running(RuntimeSerial{44}) &&
                    !unsupported.request_pause() && !unsupported.request_step(),
                "a protocol-compatible target can run without exposing unavailable pause or Step controls");
        require(unsupported.request_stop() && unsupported.finish_stop_after_reap(),
                "a session without play controls can still stop and release its channels");

        PlaySession withInput;
        FakeTransport withInputTransport;
        require(withInput.begin_configuring(launch_identity(), "0123456789abcdef") &&
                    withInput.begin_building() && withInput.begin_catalog_check(),
                "input-forwarding fixture reaches catalog negotiation");
        auto inputForwarding = play_capabilities();
        inputForwarding.inputForwarding = true;
        require(withInput.begin_starting(inputForwarding, std::move(withInputTransport.pair)),
                "v1 accepts a capability set that enables input forwarding");

        PlaySession bounded;
        FakeTransport transport(true);
        const auto mappingClosed = transport.mappingClosed;
        advance_to_starting(bounded, transport, play_capabilities(true));
        require(bounded.mark_running(RuntimeSerial{44}), "frame-capable child becomes Ready");
        std::size_t accepted = 0;
        for (std::size_t index = 0; index < MaximumQueuedMessages; ++index)
            accepted += bounded.request_capture(true) ? 1U : 0U;
        require(accepted == MaximumQueuedMessages - 8 &&
                    bounded.queued_commands() == MaximumQueuedMessages - 8 &&
                    bounded.queued_command_bytes() <= MaximumQueuedBytes,
                "optional capture requests preserve eight reliable control slots in the bounded queue");
        require(!bounded.last_error().empty(), "queue saturation is reported to the owner");
        require(bounded.request_stop(), "a saturated optional queue still permits cleanup to begin");
        require(bounded.finish_stop_after_reap(), "bounded sessions clean up after reap");
        require(*mappingClosed, "frame mapping is closed during post-reap cleanup");

        PlaySession missingMapping;
        FakeTransport noMapping;
        require(missingMapping.begin_configuring(launch_identity(), "0123456789abcdef") &&
                    missingMapping.begin_building() && missingMapping.begin_catalog_check(),
                "mapping fixture reaches catalog negotiation");
        require(!missingMapping.begin_starting(play_capabilities(true), std::move(noMapping.pair)),
                "frame streaming is rejected unless the fixed two-slot mapping is present");
    }

    void test_event_eof_is_visible_and_waits_for_reap()
    {
        PlaySession session;
        FakeTransport transport;
        advance_to_starting(session, transport);
        require(session.mark_running(RuntimeSerial{44}), "EOF fixture starts a child runtime");
        transport.eventPipe->closed = true;
        session.poll_events();
        require(session.state() == PlaySessionState::Stopping &&
                    session.last_error().find("closed") != std::string::npos,
                "unexpected event EOF becomes a visible failure instead of waiting on the UI thread");
        require(session.finish_stop_after_reap() && session.state() == PlaySessionState::Idle,
                "EOF cleanup releases session identity after child reap");
    }

    void test_coalesced_events_remain_bounded_without_being_dropped()
    {
        PlaySession session;
        FakeTransport transport;
        transport.eventPipe->maximumRead = 16U * 1024U;
        advance_to_starting(session, transport);
        const auto generation = session.generation();
        emit_event(*transport.eventPipe, session.nonce(), generation.value, 1, 44, "ready");
        for (std::uint64_t sequence = 2; sequence <= 70; ++sequence)
            emit_event(*transport.eventPipe, session.nonce(), generation.value, sequence, 44,
                       "capabilities", child_capabilities_body());

        session.poll_events();
        require(session.queued_events() == PlaySession::MaximumEventsPerCall,
                "one owner-thread pump publishes no more than its event budget");
        std::size_t drained = 0;
        while (session.take_event())
            ++drained;
        session.poll_events();
        while (session.take_event())
            ++drained;
        require(drained == 70 && session.state() == PlaySessionState::Running &&
                    session.last_error().empty(),
                "coalesced events beyond one pump budget remain buffered and arrive on the next pump");
        require(session.request_stop() && session.finish_stop_after_reap(),
                "coalesced-event fixture completes normal cleanup");
    }

    void test_parent_copies_only_owned_frame_slots_and_releases_after_copy()
    {
        PlaySession session;
        FakeTransport transport(true);
        const auto mapping = transport.mapping->bytes();
        constexpr std::size_t slotBytes = 640U * 360U * 4U;
        const std::size_t slotOffset = slotBytes;
        std::fill(mapping.begin() + static_cast<std::ptrdiff_t>(slotOffset),
                  mapping.begin() + static_cast<std::ptrdiff_t>(slotOffset + slotBytes), std::byte{0x37});
        mapping[slotOffset + slotBytes - 1] = std::byte{0xa5};
        advance_to_starting(session, transport, play_capabilities(true));
        require(session.mark_running(RuntimeSerial{44}), "frame parent becomes Ready");

        const auto generation = session.generation();
        emit_event(*transport.eventPipe, session.nonce(), generation.value, 1, 44, "frame_ready",
                   Json{{"slot", 1}, {"frame_sequence", 1}, {"width", 640}, {"height", 360}, {"stride", 2560}, {"format", "rgba8"}});
        session.poll_events();
        const auto frame = session.take_event();
        require(frame && frame->type == "frame_ready",
                "a valid current-generation frame notification enters the bounded inbox");
        std::vector<std::byte> pixels(slotBytes);
        require(!session.release_frame(1, 1),
                "parent cannot release a frame slot before copying its pixels");
        require(session.copy_frame_slot(1, 1, pixels) && pixels.front() == std::byte{0x37} &&
                    pixels.back() == std::byte{0xa5},
                "parent copies the complete owned RGBA slot into renderer-independent pixels");
        require(session.release_frame(1, 1) && !session.copy_frame_slot(1, 1, pixels),
                "a copied frame is released to the child exactly once");
        session.flush_commands();
        const auto commands = decode_requests(*transport.commandPipe);
        require(commands.size() == 1 && commands[0].type == "release_frame" &&
                    commands[0].body.value("slot", std::uint32_t{}) == 1 &&
                    commands[0].body.value("frame_sequence", std::uint64_t{}) == 1,
                "frame release identifies its exact slot incarnation over the command channel");
        emit_event(*transport.eventPipe, session.nonce(), generation.value, 2, 44, "frame_released",
                   Json{{"slot", 1}, {"frame_sequence", 1}, {"request_sequence", commands[0].sequence}});
        session.poll_events();
        const auto releaseAck = session.take_event();
        require(releaseAck && releaseAck->type == "frame_released" &&
                    !session.copy_frame_slot(1, 1, pixels),
                "the parent retires a slot only after the child's matching release acknowledgement");
        require(session.request_stop() && session.finish_stop_after_reap(),
                "frame transport closes after the session is stopped");
    }

#if !defined(_WIN32) && PLATFORMATOR_ENABLE_EDITOR_SESSION
    void test_game_panel_upload_visibility_controls_and_read_only_semantics()
    {
        const bool ownsSdlVideo = (SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0;
        if (ownsSdlVideo)
            require(SDL_InitSubSystem(SDL_INIT_VIDEO), "SDL video initializes for Game panel renderer test");
        SDL_Surface *surface = SDL_CreateSurface(GamePanel::FrameWidth, GamePanel::FrameHeight,
                                                 SDL_PIXELFORMAT_RGBA32);
        require(surface != nullptr, "software renderer test surface is created");
        SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(surface);
        require(renderer != nullptr, "Game panel receives an independent software renderer");

        PlaySession session;
        FakeTransport transport(true);
        const auto mapping = transport.mapping->bytes();
        constexpr std::size_t slotBytes = GamePanel::FrameBytes;
        std::fill_n(mapping.begin(), slotBytes, std::byte{0x6b});
        advance_to_starting(session, transport, play_capabilities(true));
        require(session.mark_running(RuntimeSerial{44}), "Game panel fixture enters Running");

        GamePanel panel;
        panel.set_renderer(renderer);
        panel.set_visible(&session, false);
        require(session.queued_commands() == 0,
                "a hidden Game view does not request runtime frame capture");
        panel.set_visible(&session, true);
        session.flush_commands();
        const auto captureRequests = decode_requests(*transport.commandPipe);
        require(captureRequests.size() == 1 && captureRequests[0].type == "set_capture" &&
                    captureRequests[0].body.value("visible", false),
                "a visible Game panel explicitly enables bounded frame capture");

        const auto generation = session.generation();
        const auto currentStatus = panel.status();
        Message staleGeneration;
        staleGeneration.nonce = session.nonce();
        staleGeneration.playGeneration = generation.value - 1;
        staleGeneration.type = "error";
        staleGeneration.body = Json{{"message", "stale generation"}};
        panel.consume_event(session, staleGeneration);
        Message staleNonce = staleGeneration;
        staleNonce.playGeneration = generation.value;
        staleNonce.nonce = "fedcba9876543210";
        panel.consume_event(session, staleNonce);
        require(panel.status() == currentStatus,
                "Game ignores queued events from a replaced session generation or nonce");

        emit_event(*transport.eventPipe, session.nonce(), generation.value, 1, 44, "frame_ready",
                   Json{{"slot", 0}, {"frame_sequence", 1}, {"width", 640}, {"height", 360}, {"stride", 2560}, {"format", "rgba8"}});
        session.poll_events();
        auto frame = session.take_event();
        require(frame && frame->type == "frame_ready", "Game panel receives one current frame event");
        panel.consume_event(session, *frame);
        require(panel.status().find("640 × 360") != std::string::npos &&
                    transport.mapping->bytes().front() == std::byte{0x6b},
                "Game panel copies and uploads the fixed RGBA frame into its own renderer texture");
        session.flush_commands();
        const auto frameRequests = decode_requests(*transport.commandPipe);
        require(frameRequests.size() == 2 && frameRequests.back().type == "release_frame" &&
                    frameRequests.back().body.value("slot", std::uint32_t{}) == 0 &&
                    frameRequests.back().body.value("frame_sequence", std::uint64_t{}) == 1,
                "the shared frame slot is released only after the Game renderer upload copy");

        const auto semantic = panel.semantic_nodes(&session, "test/game");
        const auto frameNode = std::find_if(semantic.begin(), semantic.end(), [](const SemanticNode &node)
                                            { return node.id == "test/game/frame"; });
        const auto pauseNode = std::find_if(semantic.begin(), semantic.end(), [](const SemanticNode &node)
                                            { return node.id == "test/game/pause"; });
        require(frameNode != semantic.end() && frameNode->value.find("aspect preserved") != std::string::npos &&
                    frameNode->help.find("capture") != std::string::npos &&
                    pauseNode != semantic.end() && pauseNode->enabled &&
                    panel.dispatch_semantic_action(session,
                                                   SemanticAction{HostLayoutId{1}, WindowHostGeneration{1}, 1, "test/game/pause", "activate", {}}),
                "Game semantic nodes expose aspect status, external native-input fallback, and Pause");
        session.flush_commands();
        const auto pauseRequests = decode_requests(*transport.commandPipe);
        require(pauseRequests.size() == 3 && pauseRequests.back().type == "pause",
                "semantic Pause queues the same nonblocking runtime control as the visible button");

        const auto pauseRequestSequence = pauseRequests.back().sequence;
        emit_event(*transport.eventPipe, session.nonce(), generation.value, 2, 44, "state",
                   Json{{"state", "paused"}, {"request_sequence", pauseRequestSequence}});
        session.poll_events();
        require(session.state() == PlaySessionState::Paused && session.take_event().has_value(),
                "the Game panel receives the matching Paused acknowledgement before enabling paused controls");
        const auto pausedSemantic = panel.semantic_nodes(&session, "test/game");
        const auto resumeNode = std::find_if(pausedSemantic.begin(), pausedSemantic.end(),
                                             [](const SemanticNode &node)
                                             { return node.id == "test/game/resume"; });
        const auto stepNode = std::find_if(pausedSemantic.begin(), pausedSemantic.end(),
                                           [](const SemanticNode &node)
                                           { return node.id == "test/game/step"; });
        require(resumeNode != pausedSemantic.end() && resumeNode->enabled &&
                    stepNode != pausedSemantic.end() && stepNode->enabled &&
                    panel.dispatch_semantic_action(session,
                                                   SemanticAction{HostLayoutId{1}, WindowHostGeneration{1}, 2, "test/game/step", "activate", {}}),
                "a Paused Game exposes an actionable single-step control and Resume");
        session.flush_commands();
        const auto stepRequests = decode_requests(*transport.commandPipe);
        require(stepRequests.size() == 4 && stepRequests.back().type == "step",
                "the semantic Step control queues one fixed simulation step through the session channel");
        emit_event(*transport.eventPipe, session.nonce(), generation.value, 3, 44, "step_completed",
                   Json{{"request_sequence", stepRequests.back().sequence}, {"step_number", 1}});
        session.poll_events();
        require(session.state() == PlaySessionState::Paused && session.completed_steps() == 1 &&
                    session.take_event().has_value(),
                "the Game panel remains Paused after one acknowledged fixed Step");
        require(panel.dispatch_semantic_action(session,
                                               SemanticAction{HostLayoutId{1}, WindowHostGeneration{1}, 3, "test/game/resume", "activate", {}}),
                "the semantic Resume control accepts a Paused session");
        session.flush_commands();
        const auto resumeRequests = decode_requests(*transport.commandPipe);
        require(resumeRequests.size() == 5 && resumeRequests.back().type == "resume",
                "the semantic Resume control queues the matching nonblocking session request");
        emit_event(*transport.eventPipe, session.nonce(), generation.value, 4, 44, "state",
                   Json{{"state", "running"}, {"request_sequence", resumeRequests.back().sequence}});
        session.poll_events();
        require(session.state() == PlaySessionState::Running && session.take_event().has_value(),
                "the Game panel's Resume action returns to Running only after acknowledgement");

        panel.set_visible(&session, false);
        session.flush_commands();
        const auto hiddenRequests = decode_requests(*transport.commandPipe);
        require(hiddenRequests.size() == 6 && hiddenRequests.back().type == "set_capture" &&
                    !hiddenRequests.back().body.value("visible", true),
                "hiding the Game panel disables further frame capture");

        require(session.request_stop() && session.finish_stop_after_reap() && *transport.mappingClosed,
                "Game session stop reaps and releases shared I/O before texture teardown");
        std::size_t restartRequests = 0;
        panel.set_restart_callback([&]
                                   { ++restartRequests; });
        const auto unsupportedReload = GamePanel::asset_change_status_message(false);
        const auto advertisedReload = GamePanel::asset_change_status_message(true);
        require(unsupportedReload.find("does not advertise") != std::string::npos &&
                    advertisedReload.find("no safe reload transaction path") != std::string::npos,
                "asset staleness status distinguishes absent capability from an unimplemented reload path");
        panel.set_pipeline_status(unsupportedReload, true);
        const auto stoppedSemantic = panel.semantic_nodes(&session, "test/game");
        const auto statusNode = std::find_if(stoppedSemantic.begin(), stoppedSemantic.end(),
                                             [](const SemanticNode &node)
                                             { return node.id == "test/game/status"; });
        const auto restartNode = std::find_if(stoppedSemantic.begin(), stoppedSemantic.end(),
                                              [](const SemanticNode &node)
                                              { return node.id == "test/game/restart"; });
        require(statusNode != stoppedSemantic.end() &&
                    statusNode->value.find("live asset reload") != std::string::npos &&
                    restartNode != stoppedSemantic.end() && restartNode->enabled &&
                    panel.dispatch_semantic_action(session,
                                                   SemanticAction{HostLayoutId{1}, WindowHostGeneration{1}, 2, "test/game/restart", "activate", {}}) &&
                    restartRequests == 1,
                "stale asset status and Build & Restart remain available through semantic controls");
        panel.set_pipeline_status(advertisedReload, true);
        const auto advertisedSemantic = panel.semantic_nodes(&session, "test/game");
        const auto advertisedStatusNode = std::find_if(advertisedSemantic.begin(), advertisedSemantic.end(),
                                                       [](const SemanticNode &node)
                                                       { return node.id == "test/game/status"; });
        require(advertisedStatusNode != advertisedSemantic.end() &&
                    advertisedStatusNode->value.find("no safe reload transaction path") != std::string::npos,
                "a target's live-reload claim cannot hide the editor's missing safe apply path");
        panel.set_renderer(nullptr);
        SDL_DestroyRenderer(renderer);
        SDL_DestroySurface(surface);
        if (ownsSdlVideo)
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }

    void set_nonblocking(int descriptor)
    {
        const int flags = ::fcntl(descriptor, F_GETFL);
        require(flags >= 0 && ::fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) == 0,
                "test endpoint must be nonblocking");
    }

    void write_frame(int descriptor, const std::vector<std::uint8_t> &frame)
    {
        std::size_t offset = 0;
        while (offset < frame.size())
        {
            const auto count = ::write(descriptor, frame.data() + offset, frame.size() - offset);
            require(count > 0, "test command frame must write to the child pipe");
            offset += static_cast<std::size_t>(count);
        }
    }

    std::vector<Message> read_events(int descriptor)
    {
        StreamDecoder decoder(Direction::Event);
        std::vector<Message> messages;
        std::array<std::uint8_t, 4096> bytes{};
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (std::chrono::steady_clock::now() < deadline)
        {
            const auto count = ::read(descriptor, bytes.data(), bytes.size());
            if (count > 0)
            {
                auto result = decoder.feed(std::span<const std::uint8_t>(bytes).first(
                    static_cast<std::size_t>(count)));
                require(result.succeeded(), "child event stream must decode: " + result.error);
                for (auto &message : result.messages)
                    messages.push_back(std::move(message));
                continue;
            }
            if (count < 0 && errno == EINTR)
                continue;
            if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            {
                if (!messages.empty())
                    return messages;
                std::this_thread::sleep_for(1ms);
                continue;
            }
            require(false, "child event stream closed before the expected response");
        }
        require(!messages.empty(), "child must publish its bounded event response");
        return messages;
    }

    int create_frame_mapping()
    {
        const std::string name = "/platformator-editor-session-test-" + std::to_string(::getpid());
        const int descriptor = ::shm_open(name.c_str(), O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
        require(descriptor >= 0, "test shared mapping must be created");
        ::shm_unlink(name.c_str());
        require(::ftruncate(descriptor, static_cast<off_t>(session_frame_mapping_limit)) == 0,
                "test shared mapping must have the exact two-slot size");
        return descriptor;
    }

    void test_child_session_validates_descriptors_and_services_owner_thread()
    {
        int commandPipe[2]{-1, -1};
        int eventPipe[2]{-1, -1};
        require(::pipe(commandPipe) == 0 && ::pipe(eventPipe) == 0,
                "test command and event channels must be created");
        set_nonblocking(commandPipe[0]);
        set_nonblocking(eventPipe[1]);
        set_nonblocking(eventPipe[0]);
        const int mapping = create_frame_mapping();
        EditorSessionLaunchOptions launch{
            static_cast<std::uintptr_t>(commandPipe[0]),
            static_cast<std::uintptr_t>(eventPipe[1]),
            static_cast<std::uintptr_t>(mapping)};
        {
            platformator::EditorSessionService service(launch);
            require((::fcntl(commandPipe[0], F_GETFD) & FD_CLOEXEC) != 0 &&
                        (::fcntl(eventPipe[1], F_GETFD) & FD_CLOEXEC) != 0 &&
                        (::fcntl(mapping, F_GETFD) & FD_CLOEXEC) != 0,
                    "validated child descriptors become close-on-exec before Runtime construction");
            require(service.frame_mapping().size() == session_frame_mapping_limit,
                    "child maps only the fixed two-slot pixel allocation");

            Message hello;
            hello.nonce = "0123456789abcdef";
            hello.playGeneration = 18;
            hello.sequence = 1;
            hello.type = "hello";
            std::string error;
            const auto helloFrame = encode_frame(hello, Direction::Request, error);
            require(error.empty(), "test hello must encode");
            write_frame(commandPipe[1], helloFrame);
            require(service.pump(72), "hello must leave a ready child session running");
            const auto handshake = read_events(eventPipe[0]);
            require(handshake.size() == 2 && handshake[0].type == "capabilities" &&
                        handshake[1].type == "ready" && handshake[0].runtimeSerial == 72 &&
                        handshake[0].body.value("play_control", false),
                    "child handshake reports enabled owner-thread controls and one Runtime serial");

            Message stop;
            stop.nonce = hello.nonce;
            stop.playGeneration = hello.playGeneration;
            stop.sequence = 2;
            stop.type = "stop";
            const auto stopFrame = encode_frame(stop, Direction::Request, error);
            require(error.empty(), "test stop must encode");
            write_frame(commandPipe[1], stopFrame);
            require(!service.pump(72), "an accepted Stop ends the owner-thread loop");
            const auto stopped = read_events(eventPipe[0]);
            require(stopped.size() == 1 && stopped[0].type == "stopped" &&
                        stopped[0].sequence == 3 && stopped[0].runtimeSerial == 72,
                    "Stop receives one sequenced completion on the independent event channel");
        }
        ::close(commandPipe[1]);
        ::close(eventPipe[0]);
    }

    void test_owner_runtime_pause_resume_and_one_fixed_step()
    {
        int commandPipe[2]{-1, -1};
        int eventPipe[2]{-1, -1};
        require(::pipe(commandPipe) == 0 && ::pipe(eventPipe) == 0,
                "control fixture command and event pipes must be created");
        set_nonblocking(commandPipe[0]);
        set_nonblocking(eventPipe[0]);
        set_nonblocking(eventPipe[1]);
        const int mapping = create_frame_mapping();
        platformator::EditorSessionService service({static_cast<std::uintptr_t>(commandPipe[0]),
                                                    static_cast<std::uintptr_t>(eventPipe[1]),
                                                    static_cast<std::uintptr_t>(mapping)});

        RuntimeOptions options;
        options.windowSettings.headless = true;
        options.windowSettings.enableRenderCulling = false;
        platformator::Runtime runtime(options);
        std::string encodeError;
        Message hello;
        hello.nonce = "0123456789abcdef";
        hello.playGeneration = 81;
        hello.sequence = 1;
        hello.type = "hello";
        write_frame(commandPipe[1], encode_frame(hello, Direction::Request, encodeError));
        require(encodeError.empty(), "runtime control hello must encode");

        std::size_t ownerCalls = 0;
        std::uint64_t latestStepNumber = 0;
        std::uint64_t stepBaseline = 0;
        bool stepRequestSent = false;
        bool stepCompleted = false;
        auto read_control_events = [&](std::size_t call)
        {
            try
            {
                return read_events(eventPipe[0]);
            }
            catch (const std::exception &error)
            {
                throw std::runtime_error("owner call " + std::to_string(call) +
                                         ", latest step " + std::to_string(latestStepNumber) +
                                         ": " + error.what());
            }
        };
        runtime.run_with_owner_service(
            [&](SDLWindow &window)
            {
                ++ownerCalls;
                const bool keepRunning = service.pump(runtime.getRuntimeSerial(), window);
                if (ownerCalls == 1)
                {
                    const auto handshake = read_control_events(ownerCalls);
                    require(handshake.size() == 2 && handshake[0].type == "capabilities" &&
                                handshake[0].body.value("play_control", false) &&
                                handshake[1].type == "ready",
                            "runtime handshake advertises the implemented control contract");
                    Message pause = hello;
                    pause.sequence = 2;
                    pause.type = "pause";
                    write_frame(commandPipe[1], encode_frame(pause, Direction::Request, encodeError));
                }
                else if (ownerCalls == 2)
                {
                    require(keepRunning && window.isEditorSessionPaused(),
                            "Pause takes effect on the Runtime owner thread");
                    const auto acknowledgement = read_control_events(ownerCalls);
                    require(acknowledgement.size() == 1 && acknowledgement[0].type == "state" &&
                                acknowledgement[0].body.value("state", std::string{}) == "paused" &&
                                acknowledgement[0].body.value("request_sequence", std::uint64_t{}) == 2,
                            "Pause is acknowledged only after the owner thread reaches the boundary");
                    stepBaseline = latestStepNumber;
                    Message step = hello;
                    step.sequence = 3;
                    step.type = "step";
                    write_frame(commandPipe[1], encode_frame(step, Direction::Request, encodeError));
                    stepRequestSent = true;
                }
                else if (ownerCalls == 3)
                {
                    require(keepRunning && window.isEditorSessionPaused(),
                            "Step is queued while the paused owner loop remains responsive");
                }
                else if (ownerCalls == 4)
                {
                    const auto acknowledgement = read_control_events(ownerCalls);
                    require(acknowledgement.size() == 1 && acknowledgement[0].type == "step_completed" &&
                                acknowledgement[0].body.value("request_sequence", std::uint64_t{}) == 3 &&
                                acknowledgement[0].body.value("step_number", std::uint64_t{}) ==
                                    stepBaseline + 1,
                            "Step completion reports exactly the next fixed simulation step");
                    stepCompleted = true;
                    Message resume = hello;
                    resume.sequence = 4;
                    resume.type = "resume";
                    write_frame(commandPipe[1], encode_frame(resume, Direction::Request, encodeError));
                }
                else if (ownerCalls == 5)
                {
                    require(keepRunning && !window.isEditorSessionPaused(),
                            "Resume returns the owner loop to real-time simulation");
                    const auto acknowledgement = read_control_events(ownerCalls);
                    require(acknowledgement.size() == 1 && acknowledgement[0].type == "state" &&
                                acknowledgement[0].body.value("state", std::string{}) == "running" &&
                                acknowledgement[0].body.value("request_sequence", std::uint64_t{}) == 4,
                            "Resume is acknowledged after clearing paused state");
                    Message stop = hello;
                    stop.sequence = 5;
                    stop.type = "stop";
                    write_frame(commandPipe[1], encode_frame(stop, Direction::Request, encodeError));
                }
                else if (ownerCalls == 6)
                {
                    require(latestStepNumber == stepBaseline + 1,
                            "Resume does not replay paused wall-clock debt");
                    require(!keepRunning, "Stop request is serviced by the owner loop");
                    return keepRunning;
                }
                else
                    require(false, "owner loop did not stop after the Stop request; calls=" +
                                       std::to_string(ownerCalls));
                return keepRunning;
            },
            [&](std::uint64_t stepNumber)
            {
                latestStepNumber = stepNumber;
                if (stepRequestSent && !stepCompleted)
                    require(stepNumber == stepBaseline + 1,
                            "an accepted Step runs one existing fixed step, without catch-up");
                service.simulation_step_completed(runtime.getRuntimeSerial(), stepNumber);
            });
        service.finish(runtime.getRuntimeSerial());
        const auto stopped = read_events(eventPipe[0]);
        require(stopped.size() == 1 && stopped[0].type == "stopped" && stopped[0].sequence == 6,
                "Stop ends the owner loop and emits one final child event");
        require(ownerCalls == 6 && stepCompleted,
                "the runtime services the control sequence without extra owner-loop iterations");
        ::close(commandPipe[1]);
        ::close(eventPipe[0]);
    }

    void test_hidden_frame_capture_is_visibility_gated_and_released()
    {
        int commandPipe[2]{-1, -1};
        int eventPipe[2]{-1, -1};
        require(::pipe(commandPipe) == 0 && ::pipe(eventPipe) == 0,
                "frame fixture command and event pipes must be created");
        set_nonblocking(commandPipe[0]);
        set_nonblocking(eventPipe[0]);
        set_nonblocking(eventPipe[1]);
        const int mapping = create_frame_mapping();
        platformator::EditorSessionService service({static_cast<std::uintptr_t>(commandPipe[0]),
                                                    static_cast<std::uintptr_t>(eventPipe[1]),
                                                    static_cast<std::uintptr_t>(mapping)});

        RuntimeOptions options;
        options.windowSettings.enableRenderCulling = false;
        options.editorSession = EditorSessionLaunchOptions{
            static_cast<std::uintptr_t>(commandPipe[0]),
            static_cast<std::uintptr_t>(eventPipe[1]),
            static_cast<std::uintptr_t>(mapping)};
        platformator::Runtime runtime(options);
        Message hello;
        hello.nonce = "0123456789abcdef";
        hello.playGeneration = 82;
        hello.sequence = 1;
        hello.type = "hello";
        std::string encodeError;
        write_frame(commandPipe[1], encode_frame(hello, Direction::Request, encodeError));
        require(encodeError.empty(), "frame capture hello must encode");

        std::size_t ownerCalls = 0;
        Message frameReady;
        runtime.run_with_owner_service(
            [&](SDLWindow &window)
            {
                ++ownerCalls;
                const bool keepRunning = service.pump(runtime.getRuntimeSerial(), window);
                if (ownerCalls == 1)
                {
                    require(window.getRenderWidth() == 640 && window.getRenderHeight() == 360 &&
                                (SDL_GetWindowFlags(window.getWindow()) & SDL_WINDOW_HIDDEN) != 0,
                            "session Runtime uses a hidden 640x360 renderer");
                    const auto handshake = read_events(eventPipe[0]);
                    require(handshake.size() == 2 && handshake[0].type == "capabilities" &&
                                handshake[0].body.contains("rgba_frame_stream") &&
                                handshake[1].type == "ready",
                            "session handshake advertises the bounded frame stream");
                    Message capture = hello;
                    capture.sequence = 2;
                    capture.type = "set_capture";
                    capture.body = Json{{"visible", true}};
                    write_frame(commandPipe[1], encode_frame(capture, Direction::Request, encodeError));
                }
                else if (ownerCalls == 2)
                {
                    std::array<std::uint8_t, 1> probe{};
                    errno = 0;
                    require(::read(eventPipe[0], probe.data(), probe.size()) < 0 &&
                                (errno == EAGAIN || errno == EWOULDBLOCK),
                            "the child performs no readback while capture visibility is still false");
                }
                else if (ownerCalls == 3)
                {
                    const auto events = read_events(eventPipe[0]);
                    const auto found = std::find_if(events.begin(), events.end(), [](const Message &event)
                                                    { return event.type == "frame_ready"; });
                    require(found != events.end(), "visible Game output publishes a complete frame slot");
                    frameReady = *found;
                    const auto slot = frameReady.body.at("slot").get<std::uint64_t>();
                    const auto pixels = service.frame_mapping();
                    constexpr std::size_t frameBytes = 640U * 360U * 4U;
                    const std::size_t offset = static_cast<std::size_t>(slot) * frameBytes;
                    require(offset + 4 <= pixels.size() && pixels[offset] == std::byte{0xff} &&
                                pixels[offset + 1] == std::byte{0xff} &&
                                pixels[offset + 2] == std::byte{0xff} &&
                                pixels[offset + 3] == std::byte{0xff},
                            "the shared slot contains converted RGBA8 pixels from the completed render");
                    Message release = hello;
                    release.sequence = 3;
                    release.type = "release_frame";
                    release.body = Json{{"slot", slot},
                                        {"frame_sequence", frameReady.body.at("frame_sequence")}};
                    write_frame(commandPipe[1], encode_frame(release, Direction::Request, encodeError));
                }
                else if (ownerCalls == 4)
                {
                    const auto events = read_events(eventPipe[0]);
                    const auto releasedIt = std::find_if(events.begin(), events.end(), [](const Message &e)
                                                         { return e.type == "frame_released"; });
                    require(releasedIt != events.end() &&
                                releasedIt->body.at("frame_sequence") == frameReady.body.at("frame_sequence"),
                            "the child confirms slot release before reuse");
                    Message hide = hello;
                    hide.sequence = 4;
                    hide.type = "set_capture";
                    hide.body = Json{{"visible", false}};
                    write_frame(commandPipe[1], encode_frame(hide, Direction::Request, encodeError));
                    Message stop = hello;
                    stop.sequence = 5;
                    stop.type = "stop";
                    write_frame(commandPipe[1], encode_frame(stop, Direction::Request, encodeError));
                }
                else if (ownerCalls == 5)
                    return keepRunning;
                else
                    require(false, "frame session did not stop after its owned slot was released");
                return keepRunning;
            },
            [&](std::uint64_t stepNumber)
            {
                service.simulation_step_completed(runtime.getRuntimeSerial(), stepNumber);
            });
        service.finish(runtime.getRuntimeSerial());
        const auto stopped = read_events(eventPipe[0]);
        const auto stoppedIt = std::find_if(stopped.begin(), stopped.end(), [](const Message &e)
                                            { return e.type == "stopped"; });
        require(ownerCalls == 5 && stoppedIt != stopped.end(),
                "hidden frame session stops after releasing its published slot");
        ::close(commandPipe[1]);
        ::close(eventPipe[0]);
    }

    void test_owner_thread_hierarchy_pages_and_handle_inspection()
    {
        (void)SessionInspectionBehavior::platformatorRegisteredBehavior;
        int commandPipe[2]{-1, -1};
        int eventPipe[2]{-1, -1};
        require(::pipe(commandPipe) == 0 && ::pipe(eventPipe) == 0,
                "inspection fixture command and event channels must be created");
        set_nonblocking(commandPipe[0]);
        set_nonblocking(eventPipe[0]);
        set_nonblocking(eventPipe[1]);
        const int mapping = create_frame_mapping();
        const EditorSessionLaunchOptions launch{static_cast<std::uintptr_t>(commandPipe[0]),
                                                static_cast<std::uintptr_t>(eventPipe[1]),
                                                static_cast<std::uintptr_t>(mapping)};
        platformator::EditorSessionService service(launch);

        RuntimeOptions options;
        options.windowSettings.enableRenderCulling = false;
        options.editorSession = launch;
        platformator::Runtime runtime(options);
        GameObject *root = runtime.createGameObject();
        root->setName("inspection root");
        root->setTag("live");
        root->setPosition(Eigen::Vector2f{12.5f, -3.0f});
        root->addComponent<Rigidbody>();
        root->addComponent<ScriptComponent>();
        auto *scripts = root->getComponent<ScriptComponent>();
        scripts->addBehavior(new SessionInspectionBehavior());
        GameObject *child = runtime.createGameObject();
        child->setName("inspection child");
        root->addChild(child);

        struct TemporarySceneFile
        {
            std::filesystem::path path;
            ~TemporarySceneFile()
            {
                std::error_code error;
                std::filesystem::remove(path, error);
            }
        } authoredScene{std::filesystem::temp_directory_path() /
                        ("platformator-session-inspection-" + std::to_string(::getpid()) + ".scene")};
        runtime.saveScene(authoredScene.path.string());
        scripts->addBehavior(new OpaqueSessionInspectionBehavior());
        const auto read_scene_bytes = [&]()
        {
            std::ifstream input(authoredScene.path, std::ios::binary);
            require(input.good(), "inspection fixture must retain its authored scene file");
            return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        };
        const auto authoredSceneBefore = read_scene_bytes();

        Message hello;
        hello.nonce = "0123456789abcdef";
        hello.playGeneration = 84;
        hello.sequence = 1;
        hello.type = "hello";
        std::string encodeError;
        write_frame(commandPipe[1], encode_frame(hello, Direction::Request, encodeError));
        require(encodeError.empty(), "inspection fixture hello must encode");

        std::size_t ownerCalls = 0;
        std::uint64_t firstSnapshotRevision = 0;
        platformator::ObjectHandle rootHandle;
        platformator::ObjectHandle staleHandle;
        runtime.run_with_owner_service([&](SDLWindow &window)
                                       {
        ++ownerCalls;
        const bool keepRunning = service.pump(runtime.getRuntimeSerial(), window);
        const auto send = [&](std::uint64_t sequence, std::string type, Json body = Json::object()) {
            Message request = hello;
            request.sequence = sequence;
            request.type = std::move(type);
            request.body = std::move(body);
            write_frame(commandPipe[1], encode_frame(request, Direction::Request, encodeError));
            require(encodeError.empty(), "inspection request must encode");
        };
        if (ownerCalls == 1)
        {
            const auto handshake = read_events(eventPipe[0]);
            require(handshake.size() == 2 && handshake[0].type == "capabilities" &&
                        handshake[0].body.value("runtime_inspection", false) &&
                        handshake[1].type == "ready",
                    "child advertises runtime inspection only after its owner-thread handlers are ready");
            send(2, "list_objects", {{"runtime_serial", runtime.getRuntimeSerial()}, {"page_size", 1}});
        }
        else if (ownerCalls == 2)
        {
            const auto events = read_events(eventPipe[0]);
            require(events.size() == 1 && events[0].type == "object_page" &&
                        events[0].body.value("status", std::string{}) == "ok" &&
                        events[0].body.at("objects").size() == 1 &&
                        events[0].body.at("objects")[0].at("name") == "inspection root" &&
                        events[0].body.at("objects")[0].at("sibling_index") == 0,
                    "first hierarchy page contains a bounded root summary and live handle");
            firstSnapshotRevision = events[0].body.at("snapshot_revision").get<std::uint64_t>();
            send(3, "list_objects", {{"runtime_serial", runtime.getRuntimeSerial()},
                                     {"snapshot_revision", firstSnapshotRevision},
                                     {"cursor", 1}, {"page_size", 1}});
            (void)runtime.createGameObject()->setName("new root during pagination");
        }
        else if (ownerCalls == 3)
        {
            const auto events = read_events(eventPipe[0]);
            require(events.size() == 1 && events[0].type == "object_page" &&
                        events[0].body.value("status", std::string{}) == "stale" &&
                        events[0].body.value("snapshot_revision", std::uint64_t{}) ==
                            firstSnapshotRevision,
                    "a hierarchy change invalidates a page continuation instead of mixing snapshots");
            send(4, "list_objects", {{"runtime_serial", runtime.getRuntimeSerial()}, {"page_size", 64}});
        }
        else if (ownerCalls == 4)
        {
            const auto events = read_events(eventPipe[0]);
            require(events.size() == 1 && events[0].type == "object_page" &&
                        events[0].body.value("status", std::string{}) == "ok" &&
                        events[0].body.at("objects").size() == 3,
                    "a fresh hierarchy request returns a complete new immutable page");
            const auto rootEntry = std::find_if(events[0].body.at("objects").begin(),
                events[0].body.at("objects").end(), [](const Json &entry) {
                    return entry.value("name", std::string{}) == "inspection root";
                });
            require(rootEntry != events[0].body.at("objects").end(),
                    "fresh hierarchy page retains the selected object's runtime handle");
            const auto &handle = rootEntry->at("handle");
            rootHandle = {handle.at("slot").get<std::uint32_t>(),
                          handle.at("generation").get<std::uint64_t>(),
                          handle.at("runtime_serial").get<std::uint64_t>()};
            staleHandle = rootHandle;
            send(5, "inspect_object", {{"runtime_serial", runtime.getRuntimeSerial()},
                                        {"handle", handle}});
        }
        else if (ownerCalls == 5)
        {
            const auto events = read_events(eventPipe[0]);
            require(events.size() == 1 && events[0].type == "object_snapshot" &&
                        events[0].body.value("status", std::string{}) == "ok" &&
                        events[0].body.at("handle").at("slot") == rootHandle.slot &&
                        events[0].body.at("position").at("x") == 12.5 &&
                        events[0].body.at("tag") == "live" &&
                        events[0].body.at("components").size() == 2,
                    "selected-object snapshot contains bounded built-in values and no authored mutation");
            const auto &components = events[0].body.at("components");
            const auto script = std::find_if(components.begin(), components.end(), [](const Json &entry) {
                return entry.value("type", std::string{}) == "script_component";
            });
            require(script != components.end() && script->at("properties").at("behaviors").size() == 2,
                    "script component exposes generated and opaque behaviors without serialization");
            const auto &fields = script->at("properties").at("behaviors")[0].at("fields");
            const auto score = std::find_if(fields.begin(), fields.end(), [](const Json &field) {
                return field.value("name", std::string{}) == "score";
            });
            require(score != fields.end() && score->value("available", false) &&
                        score->at("value") == 73,
                    "generated read-only field access returns the typed live value");
            const auto &opaque = script->at("properties").at("behaviors")[1];
            require(!opaque.value("provider_available", true) &&
                        opaque.value("type", std::string{}) == "OpaqueSessionInspectionBehavior",
                    "custom behavior without a generated provider is reported as unavailable");
            runtime.destroyGameObject(root);
            GameObject *replacement = runtime.createGameObject();
            const auto replacementHandle = replacement->getHandle();
            require(replacementHandle &&
                        (replacementHandle.slot != staleHandle.slot ||
                         replacementHandle.generation != staleHandle.generation),
                    "a recreated runtime object receives a distinct handle incarnation");
            send(6, "inspect_object", {{"runtime_serial", runtime.getRuntimeSerial()},
                                        {"handle", {{"runtime_serial", staleHandle.runtimeSerial},
                                                     {"slot", staleHandle.slot},
                                                     {"generation", staleHandle.generation},
                                                     {"kind", "game_object"}}}});
        }
        else if (ownerCalls == 6)
        {
            const auto events = read_events(eventPipe[0]);
            require(events.size() == 1 && events[0].type == "object_snapshot" &&
                        events[0].body.value("status", std::string{}) == "stale",
                    "destroyed handles are rejected before any object getter is called");
            send(7, "stop");
        }
        else if (ownerCalls == 7)
            return keepRunning;
        else
            require(false, "inspection session did not stop after stale handle rejection");
        return keepRunning; }, [&](std::uint64_t stepNumber)
                                       { service.simulation_step_completed(runtime.getRuntimeSerial(), stepNumber); });
        service.finish(runtime.getRuntimeSerial());
        const auto stopped = read_events(eventPipe[0]);
        require(ownerCalls == 7 && stopped.size() == 1 && stopped[0].type == "stopped",
                "inspection fixture ends through the normal Stop acknowledgement");
        require(read_scene_bytes() == authoredSceneBefore,
                "read-only session inspection and stale-handle rejection never save or modify the authored scene");
        ::close(commandPipe[1]);
        ::close(eventPipe[0]);
    }

    void test_child_never_reuses_published_frame_slots()
    {
        int commandPipe[2]{-1, -1};
        int eventPipe[2]{-1, -1};
        require(::pipe(commandPipe) == 0 && ::pipe(eventPipe) == 0,
                "backpressure fixture command and event channels must be created");
        set_nonblocking(commandPipe[0]);
        set_nonblocking(eventPipe[0]);
        set_nonblocking(eventPipe[1]);
        const int mapping = create_frame_mapping();
        const EditorSessionLaunchOptions launch{static_cast<std::uintptr_t>(commandPipe[0]),
                                                static_cast<std::uintptr_t>(eventPipe[1]),
                                                static_cast<std::uintptr_t>(mapping)};
        platformator::EditorSessionService service(launch);
        RuntimeOptions options;
        options.editorSession = launch;
        options.windowSettings.enableRenderCulling = false;
        platformator::Runtime runtime(options);

        Message hello;
        hello.nonce = "0123456789abcdef";
        hello.playGeneration = 85;
        hello.sequence = 1;
        hello.type = "hello";
        std::string encodeError;
        write_frame(commandPipe[1], encode_frame(hello, Direction::Request, encodeError));
        require(encodeError.empty(), "backpressure fixture hello must encode");

        constexpr std::size_t SlotBytes = 640U * 360U * 4U;
        std::array<std::vector<std::byte>, 2> publishedPixels;
        std::array<std::uint64_t, 2> publishedSequences{};
        std::size_t frameCount = 0;
        std::size_t ownerCalls = 0;
        bool stopSent = false;
        std::chrono::steady_clock::time_point captureRequestedAt{};
        StreamDecoder eventDecoder(Direction::Event);
        std::vector<Message> receivedEvents;
        const auto drain_events = [&]()
        {
            std::array<std::uint8_t, 4096> buffer{};
            for (;;)
            {
                const auto count = ::read(eventPipe[0], buffer.data(), buffer.size());
                if (count <= 0)
                {
                    if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                        require(false, "backpressure fixture event channel must remain readable");
                    return;
                }
                const auto decoded = eventDecoder.feed(std::span<const std::uint8_t>(buffer).first(
                    static_cast<std::size_t>(count)));
                require(decoded.succeeded(), "backpressure fixture events must decode: " + decoded.error);
                for (const auto &event : decoded.messages)
                {
                    receivedEvents.push_back(event);
                    if (event.type != "frame_ready")
                        continue;
                    require(frameCount < 2, "a full pair of published slots must suppress further capture");
                    const auto slot = event.body.at("slot").get<std::size_t>();
                    require(slot < publishedPixels.size() && publishedPixels[slot].empty(),
                            "the child cannot publish a slot again before its release acknowledgement");
                    publishedSequences[slot] = event.body.at("frame_sequence").get<std::uint64_t>();
                    const auto mapped = service.frame_mapping();
                    const auto first = mapped.begin() + static_cast<std::ptrdiff_t>(slot * SlotBytes);
                    publishedPixels[slot].assign(first, first + static_cast<std::ptrdiff_t>(SlotBytes));
                    ++frameCount;
                }
            }
        };

        runtime.run_with_owner_service([&](SDLWindow &window)
                                       {
        ++ownerCalls;
        const bool keepRunning = service.pump(runtime.getRuntimeSerial(), window);
        if (ownerCalls == 1)
        {
            const auto handshake = read_events(eventPipe[0]);
            require(handshake.size() == 2 && handshake[0].type == "capabilities" &&
                        handshake[0].body.contains("rgba_frame_stream") && handshake[1].type == "ready",
                    "frame backpressure begins only after capability negotiation");
            Message capture = hello;
            capture.sequence = 2;
            capture.type = "set_capture";
            capture.body = Json{{"visible", true}};
            write_frame(commandPipe[1], encode_frame(capture, Direction::Request, encodeError));
            require(encodeError.empty(), "capture request must encode");
            captureRequestedAt = std::chrono::steady_clock::now();
        }
        else
        {
            drain_events();
            if (!stopSent && std::chrono::steady_clock::now() - captureRequestedAt >= 350ms)
            {
                Message stop = hello;
                stop.sequence = 3;
                stop.type = "stop";
                write_frame(commandPipe[1], encode_frame(stop, Direction::Request, encodeError));
                require(encodeError.empty(), "backpressure stop request must encode");
                stopSent = true;
            }
            if (!keepRunning)
            {
                drain_events();
                return false;
            }
            std::this_thread::sleep_for(20ms);
        }
        return keepRunning; }, [&](std::uint64_t stepNumber)
                                       { service.simulation_step_completed(runtime.getRuntimeSerial(), stepNumber); });
        service.finish(runtime.getRuntimeSerial());
        drain_events();
        require(stopSent && ownerCalls >= 4 && frameCount == 2 && publishedSequences[0] != 0 &&
                    publishedSequences[1] != 0 && publishedSequences[0] != publishedSequences[1],
                "visible output fills two uniquely identified slots, then skips capture under backpressure");
        const auto mapped = service.frame_mapping();
        for (std::size_t slot = 0; slot < publishedPixels.size(); ++slot)
        {
            const auto first = mapped.begin() + static_cast<std::ptrdiff_t>(slot * SlotBytes);
            require(std::equal(publishedPixels[slot].begin(), publishedPixels[slot].end(), first),
                    "a published slot remains byte-for-byte stable until the parent releases it");
        }
        require(std::count_if(receivedEvents.begin(), receivedEvents.end(), [](const Message &event)
                              { return event.type == "frame_ready"; }) == 2,
                "a full shared mapping never publishes a reused or torn frame notification");
        ::close(commandPipe[1]);
        ::close(eventPipe[0]);
    }

    void test_play_session_owns_process_and_reaps_before_channel_cleanup()
    {
        auto provider = make_posix_session_channel_provider();
        std::string error;
        auto channels = provider->create_pair(128, error);
        require(channels != nullptr && error.empty(), "owned PlaySession channel pair should be created");

        PlaySession session;
        require(session.begin_configuring(launch_identity(), "0123456789abcdef") &&
                    session.begin_building() && session.begin_catalog_check(),
                "owned-process launch must follow configuration, build, and catalog phases");
        ProcessRequest request;
        request.argv = {PLATFORMATOR_EDITOR_SESSION_PROCESS_FIXTURE};
        request.workingDirectory = std::filesystem::absolute(
                                       PLATFORMATOR_EDITOR_SESSION_PROCESS_FIXTURE)
                                       .parent_path();
        require(session.start_process(std::move(request), play_capabilities(), std::move(channels)),
                "PlaySession must launch an owned child with inherited channels");

        const auto readyDeadline = std::chrono::steady_clock::now() + 4s;
        while (session.state() == PlaySessionState::Starting &&
               std::chrono::steady_clock::now() < readyDeadline)
        {
            session.poll_process();
            std::this_thread::sleep_for(1ms);
        }
        require(session.state() == PlaySessionState::Running && session.process_running(),
                "the child handshake establishes Running while retaining process ownership; error=" +
                    session.last_error() + ", stdout=" + session.stdout_log().text() +
                    ", stderr=" + session.stderr_log().text());
        require(session.request_stop(), "owned child accepts a protocol Stop request");

        const auto reapDeadline = std::chrono::steady_clock::now() + 4s;
        while (!session.child_reaped() && std::chrono::steady_clock::now() < reapDeadline)
        {
            session.poll_process();
            std::this_thread::sleep_for(1ms);
        }
        session.poll_process();
        bool receivedStopped = false;
        while (auto event = session.take_event())
            receivedStopped = receivedStopped || event->type == "stopped";
        require(session.child_reaped() && !session.process_running() && receivedStopped,
                "Stop acknowledgement and owned child reap both complete before cleanup");
        require(session.stdout_log().text() == "play-session-stdout\n" &&
                    session.stderr_log().text() == "play-session-stderr\n",
                "child stdout and stderr are retained separately from protocol events");
        require(session.finish_stop_after_reap() && session.state() == PlaySessionState::Idle,
                "session mappings and channels are released after the process is reaped");
    }

    void test_play_session_child_crash_reaps_before_channel_cleanup()
    {
        auto provider = make_posix_session_channel_provider();
        std::string error;
        auto channels = provider->create_pair(128, error);
        require(channels != nullptr && error.empty(), "crashing PlaySession channel pair should be created");

        PlaySession session;
        require(session.begin_configuring(launch_identity(), "0123456789abcdef") &&
                    session.begin_building() && session.begin_catalog_check(),
                "crash launch must follow configuration, build, and catalog phases");
        ProcessRequest request;
        request.argv = {PLATFORMATOR_EDITOR_SESSION_PROCESS_FIXTURE, "crash"};
        request.workingDirectory = std::filesystem::absolute(
                                       PLATFORMATOR_EDITOR_SESSION_PROCESS_FIXTURE)
                                       .parent_path();
        require(session.start_process(std::move(request), play_capabilities(), std::move(channels)),
                "PlaySession must launch the process used by the crash regression");

        const auto reapDeadline = std::chrono::steady_clock::now() + 4s;
        while (!session.child_reaped() && std::chrono::steady_clock::now() < reapDeadline)
        {
            session.poll_process();
            std::this_thread::sleep_for(1ms);
        }
        session.poll_process();
        require(session.child_reaped() && !session.process_running() &&
                    session.state() == PlaySessionState::Stopping &&
                    session.last_error().find("signal") != std::string::npos,
                "an unexpectedly killed child becomes a visible failure and is reaped before cleanup");
        require(session.launch_identity().has_value(),
                "the failed launch identity remains owned until post-reap channel cleanup");
        require(session.finish_stop_after_reap() && session.state() == PlaySessionState::Idle &&
                    !session.launch_identity(),
                "a crashed child's channels and immutable launch identity are released after reap");
    }

    void test_child_session_rejects_invalid_mapping_before_runtime_startup()
    {
        int commandPipe[2]{-1, -1};
        int eventPipe[2]{-1, -1};
        require(::pipe(commandPipe) == 0 && ::pipe(eventPipe) == 0,
                "invalid-map fixture pipes must be created");
        set_nonblocking(commandPipe[0]);
        set_nonblocking(eventPipe[1]);
        const std::string name = "/platformator-editor-session-invalid-map-" +
                                 std::to_string(::getpid());
        const int mapping = ::shm_open(name.c_str(), O_CREAT | O_EXCL | O_RDWR,
                                       S_IRUSR | S_IWUSR);
        require(mapping >= 0, "invalid-map fixture shared object must be created");
        ::shm_unlink(name.c_str());
        require(::ftruncate(mapping, 4096) == 0, "invalid-map fixture must have the wrong size");
        bool rejected = false;
        try
        {
            platformator::EditorSessionService service({static_cast<std::uintptr_t>(commandPipe[0]),
                                                        static_cast<std::uintptr_t>(eventPipe[1]),
                                                        static_cast<std::uintptr_t>(mapping)});
        }
        catch (const std::exception &)
        {
            rejected = true;
        }
        require(rejected && platformator::Runtime::tryGetCurrent() == nullptr && SDL_WasInit(0) == 0,
                "an invalid inherited mapping is rejected before constructing Runtime or SDL");
        require(::fcntl(commandPipe[0], F_GETFD) == -1 && ::fcntl(eventPipe[1], F_GETFD) == -1 &&
                    ::fcntl(mapping, F_GETFD) == -1,
                "failed validation closes each inherited child endpoint exactly once");
        ::close(commandPipe[1]);
        ::close(eventPipe[0]);
    }

    void test_runner_parses_only_the_reserved_session_descriptors()
    {
        char executable[] = "session-runner";
        char option[] = "--editor-session-v1";
        char commandFlag[] = "--editor-session-command-fd";
        char commandValue[] = "3";
        char eventFlag[] = "--editor-session-event-fd";
        char eventValue[] = "4";
        char mappingFlag[] = "--editor-session-frame-fd";
        char mappingValue[] = "5";
        char *arguments[]{executable, option, commandFlag, commandValue,
                          eventFlag, eventValue, mappingFlag, mappingValue};
        const auto options = parseRuntimeOptions(8, arguments, "");
        require(options.editorSession && options.editorSession->commandReadHandle == 3 &&
                    options.editorSession->eventWriteHandle == 4 &&
                    options.editorSession->frameMappingHandle == 5,
                "session mode accepts exactly the reserved inherited descriptor layout");

        char wrongCommandValue[] = "7";
        char *wrongArguments[]{executable, option, commandFlag, wrongCommandValue,
                               eventFlag, eventValue, mappingFlag, mappingValue};
        bool rejected = false;
        try
        {
            (void)parseRuntimeOptions(8, wrongArguments, "");
        }
        catch (const std::exception &)
        {
            rejected = true;
        }
        require(rejected, "child launch rejects descriptors outside 3/4/5 before Runtime creation");
    }

    void test_runtime_owner_service_hook_stops_between_frames()
    {
        RuntimeOptions options;
        options.windowSettings.headless = true;
        options.windowSettings.enableRenderCulling = false;
        platformator::Runtime runtime(options);
        std::size_t callbacks = 0;
        runtime.run_with_owner_service([&]
                                       {
        ++callbacks;
        return false; });
        require(callbacks == 1 && platformator::Runtime::tryGetCurrent() == &runtime,
                "owner-thread servicing runs once between frames and stops without constructing a second Runtime");
    }

    void test_game_pipeline_embedded_session_start_stop()
    {
        platformator_editor_test::TemporaryDirectory temporary;
        const auto project = temporary.path() / "project";
        const auto assets = project / "assets";
        std::filesystem::create_directories(assets);
        const auto scenePath = project / "scene.scene";
        std::ofstream(scenePath) << "[]\n";
        const std::filesystem::path executable(PLATFORMATOR_EDITOR_SESSION_PROCESS_FIXTURE);

        PipelineRequest request;
        request.projectRoot = std::filesystem::canonical(project);
        request.assetRoot = std::filesystem::canonical(assets);
        request.savedScenePath = std::filesystem::canonical(scenePath);
        request.executablePath = std::filesystem::canonical(executable);
        request.configureArgv = {request.executablePath.string(), "args", "configure"};
        request.buildArgv = {request.executablePath.string(), "args", "build"};
        request.runArguments = {"--window-width", "640", "--window-height", "360"};
        request.scene = Json::array();
        request.sceneDocumentIdentity = DocumentIdentity{
            ProjectGeneration{2}, TabId{3}, DocumentGeneration{4}};
        request.sceneSavedHistoryToken = 5;
        request.sceneSavedFileIdentity = request.savedScenePath.generic_string() + ":saved-v1";
        request.identity = PipelineIdentity{request.projectRoot.string(), "debug", "session-fixture",
                                            request.executablePath.string(), "fixture-binary-v1", "fixture-source-v1"};
        const auto launchedIdentity = request.identity;
        request.catalogProtocol = "common-runner-v1";
        request.embeddedGameView = true;
        const auto expectedDocumentIdentity = *request.sceneDocumentIdentity;

        GamePipeline pipeline;
        BehaviorCatalog catalog;
        require(pipeline.start(std::move(request), catalog, 811),
                "embedded Game pipeline accepts the immutable saved scene and launches its session child");
        const auto runningDeadline = std::chrono::steady_clock::now() + 8s;
        while (pipeline.busy() && pipeline.status().stage != PipelineStage::Running &&
               std::chrono::steady_clock::now() < runningDeadline)
        {
            pipeline.poll();
            std::this_thread::sleep_for(1ms);
        }
        auto *session = pipeline.play_session();
        require(pipeline.status().stage == PipelineStage::Running && session != nullptr &&
                    session->state() == PlaySessionState::Running && session->capabilities().frameStream &&
                    session->launch_identity() &&
                    session->launch_identity()->document == expectedDocumentIdentity &&
                    pipeline.console().text().find("play-session-stdout") != std::string::npos &&
                    pipeline.console().text().find("play-session-stderr") != std::string::npos,
                "the embedded Game pipeline waits for the validated capability handshake and ready event; stage=" +
                    std::to_string(static_cast<int>(pipeline.status().stage)) + ", message=" +
                    pipeline.status().message + ", session=" +
                    (session == nullptr ? std::string("missing") : session->last_error()) +
                    ", console=" + pipeline.console().text());
        pipeline.stop();
        const auto stopDeadline = std::chrono::steady_clock::now() + 5s;
        while (pipeline.busy() && std::chrono::steady_clock::now() < stopDeadline)
        {
            pipeline.poll();
            std::this_thread::sleep_for(1ms);
        }
        require(!pipeline.busy() && pipeline.status().outcome == PipelineOutcome::Cancelled &&
                    pipeline.status().transitions == std::vector<PipelineStage>{
                                                         PipelineStage::Configuring, PipelineStage::Building, PipelineStage::QueryingCatalog,
                                                         PipelineStage::Launching, PipelineStage::Running, PipelineStage::Idle} &&
                    session->state() == PlaySessionState::Idle,
                "embedded Stop reaps the owned child, drains its session mapping, and returns the pipeline to Idle");

        PipelineRequest staleRequest;
        staleRequest.projectRoot = std::filesystem::canonical(project);
        staleRequest.assetRoot = std::filesystem::canonical(assets);
        staleRequest.savedScenePath = std::filesystem::canonical(scenePath);
        staleRequest.executablePath = std::filesystem::canonical(executable);
        staleRequest.configureArgv = {staleRequest.executablePath.string(), "args", "configure"};
        staleRequest.buildArgv = {staleRequest.executablePath.string(), "args", "build"};
        staleRequest.runArguments = {"--window-width", "640", "--window-height", "360"};
        staleRequest.scene = Json::array();
        staleRequest.sceneDocumentIdentity = expectedDocumentIdentity;
        staleRequest.sceneSavedHistoryToken = 5;
        staleRequest.sceneSavedFileIdentity = staleRequest.savedScenePath.generic_string() + ":saved-v1";
        staleRequest.identity = launchedIdentity;
        staleRequest.catalogProtocol = "common-runner-v1";
        staleRequest.embeddedGameView = true;
        require(pipeline.start(std::move(staleRequest), catalog, 812),
                "a fresh embedded session can start after the prior child has been reaped");
        const auto staleRunningDeadline = std::chrono::steady_clock::now() + 8s;
        while (pipeline.busy() && pipeline.status().stage != PipelineStage::Running &&
               std::chrono::steady_clock::now() < staleRunningDeadline)
        {
            pipeline.poll();
            std::this_thread::sleep_for(1ms);
        }
        session = pipeline.play_session();
        require(session != nullptr && pipeline.status().stage == PipelineStage::Running &&
                    session->state() == PlaySessionState::Running,
                "the project replacement fixture must reach a live child before changing its identity");
        auto changedProjectIdentity = launchedIdentity;
        changedProjectIdentity.canonicalProjectPath = (temporary.path() / "replacement-project").string();
        pipeline.update_identity(changedProjectIdentity);
        const auto staleStopDeadline = std::chrono::steady_clock::now() + 5s;
        while (pipeline.busy() && std::chrono::steady_clock::now() < staleStopDeadline)
        {
            pipeline.poll();
            std::this_thread::sleep_for(1ms);
        }
        require(!pipeline.busy() && pipeline.status().outcome == PipelineOutcome::Failed &&
                    pipeline.status().message.find("identity changed") != std::string::npos &&
                    session->state() == PlaySessionState::Idle && !session->process_running() &&
                    session->child_reaped() && !session->launch_identity(),
                "a project switch stops and reaps the embedded child, drains session state, and rejects the stale launch");
    }
#endif
    void test_embedded_game_input_forwarding_and_serialization()
    {
        PlaySession session;
        FakeTransport transport;
        auto caps = play_capabilities();
        caps.inputForwarding = true;
        advance_to_starting(session, transport, caps);
        require(session.mark_running(RuntimeSerial{101}), "session becomes running");

        EmbeddedInputEvent keyDown;
        keyDown.kind = EmbeddedInputKind::KeyDown;
        keyDown.scancode = 44;
        keyDown.keycode = 32;
        keyDown.modifiers = 1;
        require(session.send_input(keyDown), "send_input KeyDown must be accepted");

        EmbeddedInputEvent keyUp;
        keyUp.kind = EmbeddedInputKind::KeyUp;
        keyUp.scancode = 44;
        keyUp.keycode = 32;
        keyUp.modifiers = 0;
        require(session.send_input(keyUp), "send_input KeyUp must be accepted");

        EmbeddedInputEvent mouseMove;
        mouseMove.kind = EmbeddedInputKind::MouseMove;
        mouseMove.logicalX = 320.0f;
        mouseMove.logicalY = 180.0f;
        require(session.send_input(mouseMove), "send_input MouseMove must be accepted");

        EmbeddedInputEvent mouseDown;
        mouseDown.kind = EmbeddedInputKind::MouseButtonDown;
        mouseDown.mouseButton = 1;
        mouseDown.logicalX = 100.0f;
        mouseDown.logicalY = 200.0f;
        require(session.send_input(mouseDown), "send_input MouseButtonDown must be accepted");

        require(session.release_all_inputs(), "release_all_inputs must be accepted");

        session.flush_commands();
        auto requests = decode_requests(*transport.commandPipe);
        require(requests.size() == 5, "all 5 input messages must be serialized");
        require(requests[0].type == "key_down" && requests[0].body["scancode"] == 44, "key_down verified");
        require(requests[1].type == "key_up" && requests[1].body["scancode"] == 44, "key_up verified");
        require(requests[2].type == "mouse_move" && requests[2].body["x"] == 320.0f, "mouse_move verified");
        require(requests[3].type == "mouse_button" && requests[3].body["button"] == 1 && requests[3].body["down"] == true, "mouse_button verified");
        require(requests[4].type == "release_all_inputs", "release_all_inputs verified");
    }

    void test_game_panel_input_capture_lifecycle()
    {
        PlaySession session;
        FakeTransport transport;
        auto caps = play_capabilities();
        caps.inputForwarding = true;
        advance_to_starting(session, transport, caps);
        require(session.mark_running(RuntimeSerial{102}), "session becomes running");

        GamePanel panel;
        require(!panel.is_input_captured(), "initially not capturing");

        panel.capture_input();
        require(panel.is_input_captured(), "capture_input enables input capture");

        const auto semantic = panel.semantic_nodes(&session, "game");
        const auto frameNode = std::find_if(semantic.begin(), semantic.end(),
                                            [](const SemanticNode &node) { return node.id == "game/frame"; });
        require(frameNode != semantic.end(), "frame node exists");
        require(frameNode->value.find("capturing input") != std::string::npos, "frame value shows capturing input");
        require(frameNode->help.find("Escape") != std::string::npos, "frame help mentions Escape");

        SDL_Event escEvent{};
        escEvent.type = SDL_EVENT_KEY_DOWN;
        escEvent.key.scancode = SDL_SCANCODE_ESCAPE;
        panel.handle_sdl_event(&session, escEvent);
        require(!panel.is_input_captured(), "Escape releases input capture");

        session.flush_commands();
        auto requests = decode_requests(*transport.commandPipe);
        require(!requests.empty() && requests.back().type == "release_all_inputs", "release_all_inputs sent on Escape");

        panel.capture_input();
        require(panel.is_input_captured(), "capture restarted");
        panel.set_visible(&session, false);
        require(!panel.is_input_captured(), "set_visible(false) releases input capture");

        panel.capture_input();
        require(panel.is_input_captured(), "capture restarted again");
        session.request_stop();
        panel.set_visible(&session, true);
        require(!panel.is_input_captured(), "stopping session releases input capture");
    }
} // namespace

int main()
{
    try
    {
        test_runner_catalog_is_runtime_only_and_option_gated();
        test_built_pipeline_identity_is_finalized_before_catalog_validation();
        test_partial_nonblocking_pause_step_resume_and_stop();
        test_stale_and_replayed_events_fail_closed();
        test_capability_and_queue_bounds();
        test_event_eof_is_visible_and_waits_for_reap();
        test_coalesced_events_remain_bounded_without_being_dropped();
        test_parent_copies_only_owned_frame_slots_and_releases_after_copy();
        test_embedded_game_input_forwarding_and_serialization();
        test_game_panel_input_capture_lifecycle();
#if !defined(_WIN32) && PLATFORMATOR_ENABLE_EDITOR_SESSION
        test_game_panel_upload_visibility_controls_and_read_only_semantics();
        test_child_session_validates_descriptors_and_services_owner_thread();
        test_owner_runtime_pause_resume_and_one_fixed_step();
        test_hidden_frame_capture_is_visibility_gated_and_released();
        test_owner_thread_hierarchy_pages_and_handle_inspection();
        test_child_never_reuses_published_frame_slots();
        test_play_session_owns_process_and_reaps_before_channel_cleanup();
        test_play_session_child_crash_reaps_before_channel_cleanup();
        test_game_pipeline_embedded_session_start_stop();
        test_child_session_rejects_invalid_mapping_before_runtime_startup();
        test_runner_parses_only_the_reserved_session_descriptors();
        test_runtime_owner_service_hook_stops_between_frames();
#endif
        std::cout << "[PASS] platformator_editor_playsession_test\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "[FAIL] platformator_editor_playsession_test: " << error.what() << '\n';
        return 1;
    }
}
