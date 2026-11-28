#pragma once

#include "platformator/objecthandle.h"
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

#include "editor/editorprotocol.h"

class SDLWindow;
struct SDL_Renderer;

namespace platformator
{
class Runtime;

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
    std::span<std::byte> frame_mapping() noexcept;

  private:
    struct PendingRequest
    {
        editor_session::Message message;
        std::size_t encodedBytes = 0;
    };

    struct HierarchySnapshotEntry
    {
        ObjectHandle handle{};
        ObjectHandle parent{};
        int fileId = -1;
        std::string name;
        bool nameTruncated = false;
        bool active = false;
        bool hasParent = false;
        std::size_t depth = 0;
        std::size_t siblingIndex = 0;

        bool operator==(const HierarchySnapshotEntry &) const noexcept = default;
    };

    bool accept_request(editor_session::Message message, std::uint64_t runtimeSerial,
                        ::SDLWindow *window);
    bool handle_hello_request(editor_session::Message message, std::uint64_t runtimeSerial,
                              ::SDLWindow *window);
    bool handle_pause_state_request(const editor_session::Message &message, std::uint64_t runtimeSerial,
                                    ::SDLWindow *window, bool pause);
    std::optional<bool> try_handle_input_request(const editor_session::Message &message, ::SDLWindow *window);
    bool handle_list_objects(const editor_session::Message &message, std::uint64_t runtimeSerial);
    bool handle_inspect_object(const editor_session::Message &message, std::uint64_t runtimeSerial);
    bool enqueue_inspection_event(std::string type, nlohmann::json body, std::uint64_t runtimeSerial);
    bool enqueue_inspection_response(const char *eventType, std::uint64_t requestSequence, std::string status,
                                     nlohmann::json details, std::uint64_t runtimeSerial);
    bool begin_inspection_request(const editor_session::Message &message, std::uint64_t runtimeSerial,
                                  const char *responseEvent, bool resetSnapshotOnStale,
                                  Runtime *&outRuntime, bool &outOk);
    void reset_hierarchy_snapshot() noexcept;
    bool pump_impl(std::uint64_t runtimeSerial, ::SDLWindow *window);
    std::optional<std::size_t> try_acquire_frame_slot(std::chrono::steady_clock::time_point now) const noexcept;
    bool readback_frame_to_slot(std::uint64_t runtimeSerial, ::SDL_Renderer *renderer, int width, int height,
                                std::size_t slotIndex) noexcept;
    void publish_frame_ready(std::uint64_t runtimeSerial, std::size_t slotIndex,
                             std::chrono::steady_clock::time_point now) noexcept;
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
    std::deque<PendingRequest> pendingRequests_;
    std::size_t pendingRequestBytes_ = 0;
    std::vector<HierarchySnapshotEntry> hierarchySnapshot_;
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
    bool helloAccepted_ = false;
    bool stopRequested_ = false;
    bool failed_ = false;
    bool stoppedEventQueued_ = false;
    std::vector<std::pair<int, int>> heldKeys_;
};
} // namespace platformator
