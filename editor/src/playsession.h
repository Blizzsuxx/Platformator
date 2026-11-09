#pragma once

#include "platform/sessionchannel.h"
#include "process.h"
#include "sessionids.h"

#include <editorprotocol.h>

#include <cstddef>
#include <cstdint>
#include <array>
#include <chrono>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace platformator_editor
{
enum class PlaySessionState
{
    Idle,
    Configuring,
    Building,
    CatalogCheck,
    Starting,
    Running,
    Paused,
    Stopping
};

struct PlaySessionLaunchIdentity
{
    DocumentIdentity document;
    std::uint64_t savedHistoryToken = 0;
    std::string savedFileIdentity;
    PipelineIdentity pipeline;

    bool operator==(const PlaySessionLaunchIdentity &) const = default;
};

class PlaySession
{
  public:
    static constexpr std::size_t MaximumPumpBytesPerCall = 256U * 1024U;
    static constexpr std::size_t MaximumEventsPerCall = 64;

    PlaySession();
    ~PlaySession() = default;
    PlaySession(const PlaySession &) = delete;
    PlaySession &operator=(const PlaySession &) = delete;

    bool begin_configuring(PlaySessionLaunchIdentity identity, std::string nonce = {});
    bool begin_building();
    bool finalize_pipeline_identity(PipelineIdentity identity);
    bool begin_catalog_check();
    bool begin_starting(platformator::editor_session::SessionCapabilities capabilities,
                       std::unique_ptr<SessionChannelPair> channels);
    bool start_process(ProcessRequest request,
                       platformator::editor_session::SessionCapabilities capabilities,
                       std::unique_ptr<SessionChannelPair> channels);
    void poll_process();
    bool mark_running(RuntimeSerial runtimeSerial);
    bool request_pause();
    bool request_resume();
    bool request_step();
    bool request_capture(bool visible);
    bool request_object_page(std::optional<std::uint64_t> cursor = std::nullopt,
                             std::optional<std::uint64_t> snapshotRevision = std::nullopt);
    bool request_object_snapshot(const Json &handle);
    bool copy_frame_slot(std::uint32_t slot, std::uint64_t frameSequence,
                         std::span<std::byte> destination) noexcept;
    bool release_frame(std::uint32_t slot, std::uint64_t frameSequence);
    bool request_stop();
    bool finish_stop_after_reap();

    // Both methods are nonblocking and are intended to run on the editor owner thread.
    void flush_commands();
    void poll_events();
    std::optional<platformator::editor_session::Message> take_event();

    PlaySessionState state() const noexcept;
    PlayGeneration generation() const noexcept;
    RuntimeSerial runtime_serial() const noexcept;
    const std::string &nonce() const noexcept;
    const std::string &last_error() const noexcept;
    const std::optional<PlaySessionLaunchIdentity> &launch_identity() const noexcept;
    const platformator::editor_session::SessionCapabilities &capabilities() const noexcept;
    std::size_t queued_commands() const noexcept;
    std::size_t queued_command_bytes() const noexcept;
    std::size_t queued_events() const noexcept;
    std::uint64_t completed_steps() const noexcept;
    bool matches(const PlaySessionLaunchIdentity &identity) const noexcept;
    bool process_running() const noexcept;
    bool child_reaped() const noexcept;
    const BoundedConsole &stdout_log() const noexcept;
    const BoundedConsole &stderr_log() const noexcept;

  private:
    using ProtocolMessage = platformator::editor_session::Message;

    bool queue_command(std::string type, Json body,
                       std::optional<std::uint64_t> *acceptedSequence = nullptr);
    bool handle_event(ProtocolMessage message);
    void fail(std::string error) noexcept;
    void reset_protocol_state() noexcept;
    static std::string make_nonce();

    platformator::editor_session::SequenceTracker eventSequences_;
    platformator::editor_session::BoundedQueue outgoing_{
        platformator::editor_session::Direction::Request};
    platformator::editor_session::StreamDecoder incoming_{
        platformator::editor_session::Direction::Event};
    MonotonicIdentitySource<PlayGeneration> generations_;
    std::optional<PlaySessionLaunchIdentity> launchIdentity_;
    platformator::editor_session::SessionCapabilities capabilities_;
    std::unique_ptr<SessionChannelPair> channels_;
    ProcessService process_;
    BoundedConsole stdoutLog_;
    BoundedConsole stderrLog_;
    bool processStarted_ = false;
    bool childReaped_ = false;
    bool processStopRequested_ = false;
    std::chrono::steady_clock::time_point stopDeadline_{};
    std::deque<ProtocolMessage> pendingDecodedEvents_;
    std::size_t pendingDecodedEventBytes_ = 0;
    std::deque<ProtocolMessage> events_;
    std::size_t eventBytes_ = 0;
    std::optional<std::uint64_t> pendingPauseSequence_;
    std::optional<std::uint64_t> pendingResumeSequence_;
    std::optional<std::uint64_t> pendingStepSequence_;
    bool inspectionRequestPending_ = false;
    std::array<std::uint64_t, 2> frameSlots_{};
    std::array<std::uint64_t, 2> frameReleaseRequests_{};
    std::array<bool, 2> frameReleasing_{};
    std::array<bool, 2> frameCopied_{};
    std::uint64_t lastFrameSequence_ = 0;
    std::uint64_t startupRuntimeSerial_ = 0;
    std::uint64_t nextRequestSequence_ = 1;
    std::uint64_t completedSteps_ = 0;
    std::uint64_t lastStepNumber_ = 0;
    PlayGeneration generation_;
    RuntimeSerial runtimeSerial_;
    PlaySessionState state_ = PlaySessionState::Idle;
    std::string nonce_;
    std::string lastError_;
};
} // namespace platformator_editor
