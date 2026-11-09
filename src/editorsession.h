#pragma once

#include "platformator/runtimeoptions.h"

#include <cstddef>
#include <cstdint>
#include <array>
#include <chrono>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "editorprotocol.h"

class SDLWindow;
struct SDL_Renderer;

namespace platformator
{

// Child-side owner-thread protocol endpoint. Construction validates and takes
// ownership of the three inherited descriptors before Runtime is created.
class EditorSessionService
{
  public:
    static constexpr std::size_t MaximumPumpBytesPerCall = 256U * 1024U;
    static constexpr std::size_t MaximumMessagesPerCall = 64;

    explicit EditorSessionService(EditorSessionLaunchOptions launch);
    ~EditorSessionService();
    EditorSessionService(const EditorSessionService &) = delete;
    EditorSessionService &operator=(const EditorSessionService &) = delete;

    // Nonblocking; call only on the Runtime owner thread between frame phases.
    bool pump(std::uint64_t runtimeSerial);
    bool pump(std::uint64_t runtimeSerial, ::SDLWindow &window);
    void simulation_step_completed(std::uint64_t runtimeSerial,
                                  std::uint64_t stepNumber) noexcept;
    void finish(std::uint64_t runtimeSerial) noexcept;
    const std::string &last_error() const noexcept { return lastError_; }
    std::span<std::byte> frame_mapping() noexcept;

  private:
    bool accept_request(editor_session::Message message, std::uint64_t runtimeSerial,
                        ::SDLWindow *window);
    bool pump_impl(std::uint64_t runtimeSerial, ::SDLWindow *window);
    void capture_frame(std::uint64_t runtimeSerial, ::SDL_Renderer *renderer,
                       int width, int height) noexcept;
    bool enqueue_event(std::string type, nlohmann::json body,
                       std::uint64_t runtimeSerial, bool reliable = true);
    bool flush_events();
    void fail(std::string error, std::uint64_t runtimeSerial) noexcept;
    void close_endpoints() noexcept;

    int commandRead_ = -1;
    int eventWrite_ = -1;
    int mappingFd_ = -1;
    void *mappingAddress_ = nullptr;
    std::size_t mappingBytes_ = 0;
    editor_session::StreamDecoder incoming_{editor_session::Direction::Request};
    editor_session::SequenceTracker requestSequences_;
    editor_session::InspectionPageGate inspectionPageGate_;
    editor_session::SessionCapabilities capabilities_;
    editor_session::BoundedQueue outgoing_{editor_session::Direction::Event};
    std::deque<editor_session::Message> pendingRequests_;
    std::size_t pendingRequestBytes_ = 0;
    std::vector<nlohmann::json> hierarchySnapshot_;
    std::uint64_t hierarchySnapshotRevision_ = 0;
    std::uint64_t nextInspectionRevision_ = 1;
    std::size_t nextHierarchyCursor_ = 0;
    std::string nonce_;
    std::uint64_t playGeneration_ = 0;
    std::uint64_t nextEventSequence_ = 1;
    std::optional<std::uint64_t> pendingStepSequence_;
    std::array<bool, 2> frameSlotBusy_{};
    std::array<std::uint64_t, 2> frameSlotSequences_{};
    std::uint64_t nextFrameSequence_ = 1;
    std::chrono::steady_clock::time_point lastFrameCapture_{};
    bool captureVisible_ = false;
    std::string lastError_;
    bool helloAccepted_ = false;
    bool stopRequested_ = false;
    bool failed_ = false;
    bool stoppedEventQueued_ = false;
};
} // namespace platformator
