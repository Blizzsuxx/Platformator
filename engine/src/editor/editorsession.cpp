#include "editor/editorsession.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <limits>
#include <memory>
#include <stdexcept>

#include "platformator/scriptregistration.h"
#include "rendering/sdlwindow.h"

#if !defined(_WIN32)
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace platformator
{
namespace
{
constexpr std::size_t FrameMappingBytes = 1'843'200;
constexpr std::size_t FrameSlotBytes = 921'600;
constexpr std::size_t FrameWidth = 640;
constexpr std::size_t FrameHeight = 360;
constexpr std::size_t FrameStride = FrameWidth * 4;
constexpr std::size_t ReadChunkBytes = 16U * 1024U;
constexpr auto MinimumCaptureInterval = std::chrono::microseconds(16'667);
constexpr std::size_t ReservedFrameMessages = 8;
constexpr std::size_t ReservedFrameBytes = 16U * 1024U;

#if !defined(_WIN32)
std::string system_error(const char *operation, int error)
{
    return std::string(operation) + ": " + std::strerror(error);
}

bool validate_pipe_descriptor(int descriptor, int requiredAccess, const char *label, std::string &error)
{
    const int descriptorFlags = ::fcntl(descriptor, F_GETFD);
    const int statusFlags = ::fcntl(descriptor, F_GETFL);
    struct stat status{};
    if (descriptorFlags < 0 || statusFlags < 0 || ::fstat(descriptor, &status) != 0)
    {
        error = system_error(label, errno);
        return false;
    }
    if (!S_ISFIFO(status.st_mode) || (statusFlags & O_ACCMODE) != requiredAccess || (statusFlags & O_NONBLOCK) == 0)
    {
        error = std::string(label) + " must be a nonblocking pipe with the expected access mode";
        return false;
    }
    if (::fcntl(descriptor, F_SETFD, descriptorFlags | FD_CLOEXEC) < 0)
    {
        error = system_error("cannot make child session descriptor close-on-exec", errno);
        return false;
    }
    return true;
}

enum class WriteStatus
{
    Progress,
    WouldBlock,
    Closed,
    Failed
};

struct WriteResult
{
    WriteStatus status = WriteStatus::WouldBlock;
    std::size_t bytes = 0;
    std::string error;
};

WriteResult write_without_sigpipe(int descriptor, std::span<const std::uint8_t> bytes)
{
    if (bytes.empty())
        return {WriteStatus::Progress, 0, {}};
    sigset_t blocked{};
    sigset_t previous{};
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGPIPE);
    const int maskError = ::pthread_sigmask(SIG_BLOCK, &blocked, &previous);
    if (maskError != 0)
        return {WriteStatus::Failed, 0, system_error("cannot block SIGPIPE for session write", maskError)};
    sigset_t pending{};
    const bool wasPending = ::sigpending(&pending) == 0 && sigismember(&pending, SIGPIPE) == 1;

    ssize_t count = -1;
    int writeError = 0;
    for (;;)
    {
        count = ::write(descriptor, bytes.data(), bytes.size());
        if (count >= 0 || errno != EINTR)
        {
            if (count < 0)
                writeError = errno;
            break;
        }
    }
    if (writeError == EPIPE && !wasPending)
    {
        int signalNumber = 0;
        ::sigwait(&blocked, &signalNumber);
    }
    const int restoreError = ::pthread_sigmask(SIG_SETMASK, &previous, nullptr);
    if (restoreError != 0)
        return {WriteStatus::Failed, 0, system_error("cannot restore signal mask after session write", restoreError)};
    if (count >= 0)
        return {WriteStatus::Progress, static_cast<std::size_t>(count), {}};
    if (writeError == EAGAIN || writeError == EWOULDBLOCK)
        return {WriteStatus::WouldBlock, 0, {}};
    if (writeError == EPIPE)
        return {WriteStatus::Closed, 0, {}};
    return {WriteStatus::Failed, 0, system_error("session event write failed", writeError)};
}
#endif
} // namespace

EditorSessionService::EditorSessionService(EditorSessionLaunchOptions launch)
{
#if defined(_WIN32)
    (void)launch;
    throw std::runtime_error("native Windows editor-session child endpoints are not available in this build");
#else
    if (launch.commandReadHandle > static_cast<std::uintptr_t>(std::numeric_limits<int>::max()) ||
        launch.eventWriteHandle > static_cast<std::uintptr_t>(std::numeric_limits<int>::max()) ||
        launch.frameMappingHandle > static_cast<std::uintptr_t>(std::numeric_limits<int>::max()))
        throw std::invalid_argument("editor-session descriptor is outside the native descriptor range");
    commandRead_ = static_cast<int>(launch.commandReadHandle);
    eventWrite_ = static_cast<int>(launch.eventWriteHandle);
    mappingFd_ = static_cast<int>(launch.frameMappingHandle);
    if (commandRead_ == eventWrite_ || commandRead_ == mappingFd_ || eventWrite_ == mappingFd_)
        throw std::invalid_argument("editor-session descriptors must be distinct");

    std::string error;
    try
    {
        if (!validate_pipe_descriptor(commandRead_, O_RDONLY, "command descriptor", error) ||
            !validate_pipe_descriptor(eventWrite_, O_WRONLY, "event descriptor", error))
            throw std::invalid_argument(error);
        const int mappingDescriptorFlags = ::fcntl(mappingFd_, F_GETFD);
        const int mappingStatusFlags = ::fcntl(mappingFd_, F_GETFL);
        struct stat mappingStatus{};
        if (mappingDescriptorFlags < 0 || mappingStatusFlags < 0 || ::fstat(mappingFd_, &mappingStatus) != 0)
            throw std::invalid_argument(system_error("cannot inspect session frame mapping", errno));
        if ((mappingStatusFlags & O_ACCMODE) != O_RDWR || !S_ISREG(mappingStatus.st_mode) ||
            mappingStatus.st_size < 0 || static_cast<std::uint64_t>(mappingStatus.st_size) != FrameMappingBytes)
            throw std::invalid_argument("frame mapping descriptor must name exactly two 640x360 RGBA8 slots");
        if (::fcntl(mappingFd_, F_SETFD, mappingDescriptorFlags | FD_CLOEXEC) < 0)
            throw std::runtime_error(system_error("cannot make frame mapping close-on-exec", errno));
        mappingAddress_ = ::mmap(nullptr, FrameMappingBytes, PROT_READ | PROT_WRITE, MAP_SHARED, mappingFd_, 0);
        if (mappingAddress_ == MAP_FAILED)
        {
            mappingAddress_ = nullptr;
            throw std::runtime_error(system_error("cannot map editor-session frame slots", errno));
        }
        mappingBytes_ = FrameMappingBytes;
    }
    catch (...)
    {
        close_endpoints();
        throw;
    }
#endif
}

EditorSessionService::~EditorSessionService()
{
    close_endpoints();
}

std::span<std::byte> EditorSessionService::frame_mapping() noexcept
{
    if (mappingAddress_ == nullptr || mappingBytes_ == 0)
        return {};
    return {static_cast<std::byte *>(mappingAddress_), mappingBytes_};
}

bool EditorSessionService::pump(std::uint64_t runtimeSerial, ::SDLWindow &window)
{
    return pump_impl(runtimeSerial, &window);
}

bool EditorSessionService::pump(std::uint64_t runtimeSerial)
{
    return pump_impl(runtimeSerial, nullptr);
}

bool EditorSessionService::pump_impl(std::uint64_t runtimeSerial, ::SDLWindow *window)
{
#if defined(_WIN32)
    (void)runtimeSerial;
    (void)window;
    return false;
#else
    if (failed_ || stopRequested_)
    {
        flush_events();
        return false;
    }
    if (!flush_events())
        return false;
    std::array<std::byte, ReadChunkBytes> buffer{};
    std::size_t remaining = MaximumPumpBytesPerCall;
    std::size_t handled = 0;
    while (handled < MaximumMessagesPerCall)
    {
        if (!pendingRequests_.empty())
        {
            auto pending = std::move(pendingRequests_.front());
            pendingRequests_.pop_front();
            pendingRequestBytes_ -= std::min(pendingRequestBytes_, pending.encodedBytes);
            ++handled;
            if (!accept_request(std::move(pending.message), runtimeSerial, window))
                break;
            continue;
        }
        if (remaining == 0)
            break;
        const std::size_t readBytes = std::min(remaining, buffer.size());
        const ssize_t count = ::read(commandRead_, buffer.data(), readBytes);
        if (count > 0)
        {
            const auto transferred = static_cast<std::size_t>(count);
            remaining -= transferred;
            const auto decoded = incoming_.feed(
                std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(buffer.data()), transferred));
            if (!decoded.succeeded())
            {
                fail(decoded.error, runtimeSerial);
                break;
            }
            for (const auto &request : decoded.messages)
            {
                const std::size_t frameBytes = request.frameBytes;
                if (frameBytes == 0 ||
                    frameBytes > editor_session::MaximumQueuedBytes -
                                     std::min(pendingRequestBytes_, editor_session::MaximumQueuedBytes))
                {
                    fail("decoded request backlog exceeded its byte bound", runtimeSerial);
                    break;
                }
                pendingRequestBytes_ += frameBytes;
                pendingRequests_.push_back({request, frameBytes});
            }
            if (failed_)
                break;
            continue;
        }
        if (count == 0)
        {
            const auto finished = incoming_.finish();
            if (!finished.succeeded())
                fail(finished.error, runtimeSerial);
            else if (!stopRequested_)
                fail("editor-session command channel reached EOF", runtimeSerial);
            break;
        }
        if (errno == EINTR)
            continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            break;
        fail(system_error("editor-session command read failed", errno), runtimeSerial);
        break;
    }
    flush_events();
    return !failed_ && !stopRequested_;
#endif
}

void EditorSessionService::simulation_step_completed(std::uint64_t runtimeSerial,
                                                     std::uint64_t stepNumber) noexcept
{
    if (!pendingStepSequence_ || failed_ || !helloAccepted_ || runtimeSerial == 0 || stepNumber == 0)
        return;
    const auto requestSequence = *pendingStepSequence_;
    pendingStepSequence_.reset();
    try
    {
        if (!enqueue_event("step_completed",
                           nlohmann::json{{"request_sequence", requestSequence}, {"step_number", stepNumber}},
                           runtimeSerial, true))
        {
            fail("could not queue the accepted Step completion", runtimeSerial);
            return;
        }
        if (!flush_events())
            fail("could not flush the accepted Step completion", runtimeSerial);
    }
    catch (const std::exception &error)
    {
        fail(error.what(), runtimeSerial);
    }
    catch (...)
    {
        fail("unknown failure while acknowledging a fixed Step", runtimeSerial);
    }
}

void EditorSessionService::finish(std::uint64_t runtimeSerial) noexcept
{
    if (!helloAccepted_ || stoppedEventQueued_ || failed_)
        return;
    try
    {
        stopRequested_ = true;
        stoppedEventQueued_ = enqueue_event("stopped", nlohmann::json::object(), runtimeSerial);
        flush_events();
    }
    catch (const std::exception &error)
    {
        if (lastError_.empty())
            lastError_ = error.what();
    }
    catch (...)
    {
        if (lastError_.empty())
            lastError_ = "unknown failure while closing editor session";
    }
}

bool EditorSessionService::handle_hello_request(editor_session::Message message, std::uint64_t runtimeSerial,
                                                ::SDLWindow *window)
{
    if (message.type != "hello" || message.sequence != 1 || !requestSequences_.accept(message.sequence))
    {
        fail("the first editor-session request must be hello with sequence one", runtimeSerial);
        return false;
    }
    helloAccepted_ = true;
    nonce_ = std::move(message.nonce);
    playGeneration_ = message.playGeneration;
    const auto catalog = exportRegisteredBehaviorCatalog();
    capabilities_ = editor_session::read_session_capabilities(catalog);
    if (!capabilities_.supported)
    {
        fail(capabilities_.error.empty() ? "this runner does not expose a compatible editor-session protocol"
                                         : capabilities_.error,
             runtimeSerial);
        return false;
    }
    if (window != nullptr)
    {
        if (!window->enableEditorSessionControls())
        {
            fail("could not initialize editor-session pause controls", runtimeSerial);
            return false;
        }
        if (capabilities_.frameStream)
        {
            window->setEditorSessionFrameCapture(
                [this, runtimeSerial](::SDL_Renderer *renderer, int width, int height) {
                    capture_frame(runtimeSerial, renderer, width, height);
                });
        }
    }
    const auto capabilities = catalog.find("editor_capabilities");
    const nlohmann::json capabilityBody =
        capabilities == catalog.end() ? nlohmann::json::object() : *capabilities;
    if (!enqueue_event("capabilities", capabilityBody, runtimeSerial) ||
        !enqueue_event("ready", nlohmann::json::object(), runtimeSerial))
    {
        fail("could not queue initial session capability and ready events", runtimeSerial);
        return false;
    }
    return true;
}

bool EditorSessionService::handle_pause_state_request(const editor_session::Message &message,
                                                      std::uint64_t runtimeSerial, ::SDLWindow *window, bool pause)
{
    if (!capabilities_.playControl || window == nullptr || window->isEditorSessionPaused() == pause ||
        !window->setEditorSessionPaused(pause))
    {
        fail(pause ? "Pause is unavailable or invalid in the current session state"
                   : "Resume is unavailable or invalid in the current session state",
             runtimeSerial);
        return false;
    }
    if (!enqueue_event("state",
                       nlohmann::json{{"state", pause ? "paused" : "running"},
                                      {"request_sequence", message.sequence}},
                       runtimeSerial, true))
    {
        fail(pause ? "could not acknowledge the accepted Pause request"
                   : "could not acknowledge the accepted Resume request",
             runtimeSerial);
        return false;
    }
    return true;
}

bool EditorSessionService::handle_input_request(const editor_session::Message &message, ::SDLWindow *window)
{
    if (message.type == "release_all_inputs")
    {
        if (window)
            window->releaseEditorInput();
        for (const auto &[scancode, keycode] : heldKeys_)
        {
            SDL_Event event{};
            event.type = SDL_EVENT_KEY_UP;
            event.key.scancode = static_cast<SDL_Scancode>(scancode);
            event.key.key = static_cast<SDL_Keycode>(keycode);
            event.key.down = false;
            SDL_PushEvent(&event);
        }
        heldKeys_.clear();
        return true;
    }
    if (!capabilities_.inputForwarding)
        return false;
    if (message.type == "key_down" || message.type == "key_up")
    {
        const bool down = message.type == "key_down";
        const auto scancode = message.body.value("scancode", 0);
        const auto keycode = message.body.value("keycode", 0);
        const auto modifiers = message.body.value("modifiers", 0);
        SDL_Event event{};
        event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        event.key.scancode = static_cast<SDL_Scancode>(scancode);
        event.key.key = static_cast<SDL_Keycode>(keycode);
        event.key.mod = static_cast<SDL_Keymod>(modifiers);
        event.key.down = down;
        if (window)
            window->setEditorKey(event.key.scancode, down);
        SDL_PushEvent(&event);
        if (down)
            heldKeys_.push_back({scancode, keycode});
        else if (auto it = std::find(heldKeys_.begin(), heldKeys_.end(), std::make_pair(scancode, keycode));
                 it != heldKeys_.end())
            heldKeys_.erase(it);
        return true;
    }
    if (message.type == "mouse_move")
    {
        const auto x = message.body.value("x", 0.0f);
        const auto y = message.body.value("y", 0.0f);
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_MOTION;
        event.motion.x = x;
        event.motion.y = y;
        if (window)
            window->setEditorMouse(x, y);
        SDL_PushEvent(&event);
        return true;
    }
    if (message.type == "mouse_button")
    {
        const auto button = message.body.value("button", 1);
        const auto down = message.body.value("down", true);
        const auto x = message.body.value("x", 0.0f);
        const auto y = message.body.value("y", 0.0f);
        SDL_Event event{};
        event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        event.button.button = static_cast<Uint8>(button);
        event.button.down = down;
        event.button.x = x;
        event.button.y = y;
        if (window)
            window->setEditorMouse(x, y, button, down);
        SDL_PushEvent(&event);
        return true;
    }
    return false;
}

bool EditorSessionService::accept_request(editor_session::Message message, std::uint64_t runtimeSerial,
                                          ::SDLWindow *window)
{
    if (!helloAccepted_)
        return handle_hello_request(std::move(message), runtimeSerial, window);

    if (message.nonce != nonce_ || message.playGeneration != playGeneration_)
    {
        fail("editor-session request belongs to another nonce or play generation", runtimeSerial);
        return false;
    }
    if (!requestSequences_.accept(message.sequence))
    {
        fail("editor-session request sequence was replayed or moved backwards", runtimeSerial);
        return false;
    }
    if (message.type == "stop")
    {
        stopRequested_ = true;
        if (!stoppedEventQueued_)
            stoppedEventQueued_ = enqueue_event("stopped", nlohmann::json::object(), runtimeSerial);
        return false;
    }
    if (message.type == "pause" || message.type == "resume")
        return handle_pause_state_request(message, runtimeSerial, window, message.type == "pause");
    if (message.type == "step")
    {
        if (!capabilities_.playControl || window == nullptr || !window->isEditorSessionPaused() ||
            pendingStepSequence_ || !window->requestEditorSessionStep())
        {
            fail("Step is unavailable or invalid in the current session state", runtimeSerial);
            return false;
        }
        pendingStepSequence_ = message.sequence;
        return true;
    }
    if (message.type == "set_capture")
    {
        const auto visible = message.body.find("visible");
        if (!capabilities_.frameStream || visible == message.body.end() || !visible->is_boolean())
        {
            fail("capture visibility is unavailable or malformed", runtimeSerial);
            return false;
        }
        captureVisible_ = visible->get<bool>();
        return true;
    }
    if (message.type == "release_frame")
    {
        const auto slot = message.body.find("slot");
        const auto sequence = message.body.find("frame_sequence");
        if (slot == message.body.end() || !slot->is_number_unsigned() ||
            sequence == message.body.end() || !sequence->is_number_unsigned())
        {
            fail("frame release is missing its slot or sequence", runtimeSerial);
            return false;
        }
        const auto slotIndex = slot->get<std::uint64_t>();
        const auto frameSequence = sequence->get<std::uint64_t>();
        if (slotIndex >= frameSlotBusy_.size() || !frameSlotBusy_[slotIndex] ||
            frameSlotSequences_[slotIndex] != frameSequence)
        {
            fail("frame release names a free, reused, or stale frame slot", runtimeSerial);
            return false;
        }
        if (!enqueue_event("frame_released",
                           nlohmann::json{{"slot", slotIndex},
                                          {"frame_sequence", frameSequence},
                                          {"request_sequence", message.sequence}},
                           runtimeSerial, true))
        {
            fail("could not acknowledge release of a completed frame slot", runtimeSerial);
            return false;
        }
        frameSlotBusy_[slotIndex] = false;
        frameSlotSequences_[slotIndex] = 0;
        return true;
    }
    if (message.type == "key_down" || message.type == "key_up" || message.type == "mouse_move" ||
        message.type == "mouse_button" || message.type == "release_all_inputs")
        return handle_input_request(message, window);
    if (message.type == "list_objects")
        return handle_list_objects(message, runtimeSerial);
    if (message.type == "inspect_object")
        return handle_inspect_object(message, runtimeSerial);

    fail("unsupported editor-session command: " + message.type, runtimeSerial);
    return false;
}

void EditorSessionService::capture_frame(std::uint64_t runtimeSerial, ::SDL_Renderer *renderer,
                                         int width, int height) noexcept
{
    if (!captureVisible_ || !capabilities_.frameStream || failed_ || stopRequested_ || renderer == nullptr)
        return;
    const auto now = std::chrono::steady_clock::now();
    if (lastFrameCapture_ != std::chrono::steady_clock::time_point{} &&
        now - lastFrameCapture_ < MinimumCaptureInterval)
        return;
    if (outgoing_.size() >= editor_session::MaximumQueuedMessages - ReservedFrameMessages ||
        outgoing_.bytes() >= editor_session::MaximumQueuedBytes - ReservedFrameBytes - 512)
        return;

    std::size_t slotIndex = frameSlotBusy_.size();
    for (std::size_t index = 0; index < frameSlotBusy_.size(); ++index)
        if (!frameSlotBusy_[index])
        {
            slotIndex = index;
            break;
        }
    if (slotIndex == frameSlotBusy_.size())
        return;

    struct SlotGuard
    {
        bool &busy;
        bool active = true;
        ~SlotGuard() noexcept
        {
            if (active)
                busy = false;
        }
    };
    frameSlotBusy_[slotIndex] = true;
    SlotGuard slotGuard{frameSlotBusy_[slotIndex]};

    int outputWidth = 0;
    int outputHeight = 0;
    if (width != static_cast<int>(FrameWidth) || height != static_cast<int>(FrameHeight) ||
        !SDL_GetRenderOutputSize(renderer, &outputWidth, &outputHeight) ||
        outputWidth != static_cast<int>(FrameWidth) || outputHeight != static_cast<int>(FrameHeight))
    {
        fail("session renderer output is not exactly 640x360", runtimeSerial);
        return;
    }
    if (mappingAddress_ == nullptr || mappingBytes_ != FrameMappingBytes || nextFrameSequence_ == 0)
    {
        fail("session frame mapping or sequence is unavailable", runtimeSerial);
        return;
    }

    using SurfacePtr = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
    SurfacePtr readback(SDL_RenderReadPixels(renderer, nullptr), SDL_DestroySurface);
    if (!readback)
    {
        fail(std::string("session frame readback failed: ") + SDL_GetError(), runtimeSerial);
        return;
    }
    SurfacePtr rgba(SDL_ConvertSurface(readback.get(), SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
    readback.reset();
    if (!rgba || rgba->w != static_cast<int>(FrameWidth) || rgba->h != static_cast<int>(FrameHeight) ||
        rgba->pitch < static_cast<int>(FrameStride) || rgba->pixels == nullptr)
    {
        fail("session readback could not be converted to bounded RGBA8 pixels", runtimeSerial);
        return;
    }
    if (!SDL_LockSurface(rgba.get()))
    {
        fail(std::string("session RGBA readback could not be locked: ") + SDL_GetError(), runtimeSerial);
        return;
    }

    auto *destination = static_cast<std::byte *>(mappingAddress_) + slotIndex * FrameSlotBytes;
    const auto *source = static_cast<const std::byte *>(rgba->pixels);
    for (std::size_t row = 0; row < FrameHeight; ++row)
        std::copy_n(source + row * static_cast<std::size_t>(rgba->pitch), FrameStride,
                    destination + row * FrameStride);
    SDL_UnlockSurface(rgba.get());
    rgba.reset();
    std::atomic_thread_fence(std::memory_order_release);

    const std::uint64_t frameSequence = nextFrameSequence_;
    const bool queued = enqueue_event(
        "frame_ready",
        nlohmann::json{{"slot", slotIndex}, {"frame_sequence", frameSequence},
                       {"width", FrameWidth}, {"height", FrameHeight},
                       {"stride", FrameStride}, {"format", "rgba8"}},
        runtimeSerial, false);
    if (!queued)
        return;

    slotGuard.active = false;
    frameSlotSequences_[slotIndex] = frameSequence;
    nextFrameSequence_ =
        frameSequence == std::numeric_limits<std::uint64_t>::max() ? 0 : frameSequence + 1;
    lastFrameCapture_ = now;
    flush_events();
}

bool EditorSessionService::enqueue_event(std::string type, nlohmann::json body,
                                         std::uint64_t runtimeSerial, bool reliable)
{
    if (!helloAccepted_ || nextEventSequence_ == 0 || runtimeSerial == 0)
        return false;
    editor_session::Message event;
    event.nonce = nonce_;
    event.playGeneration = playGeneration_;
    event.sequence = nextEventSequence_;
    event.runtimeSerial = runtimeSerial;
    event.type = std::move(type);
    event.body = std::move(body);
    if (!outgoing_.enqueue(event, reliable))
        return false;
    nextEventSequence_ =
        nextEventSequence_ == std::numeric_limits<std::uint64_t>::max() ? 0 : nextEventSequence_ + 1;
    return true;
}

bool EditorSessionService::flush_events()
{
#if defined(_WIN32)
    return false;
#else
    std::size_t remaining = MaximumPumpBytesPerCall;
    while (remaining > 0)
    {
        const auto bytes = outgoing_.front();
        if (bytes.empty())
        {
            inspectionPageGate_.complete();
            return true;
        }
        const auto result =
            write_without_sigpipe(eventWrite_, bytes.first(std::min(remaining, bytes.size())));
        if (result.status == WriteStatus::WouldBlock ||
            (result.status == WriteStatus::Progress && result.bytes == 0))
            return true;
        if (result.status == WriteStatus::Progress)
        {
            if (result.bytes > bytes.size() || !outgoing_.consume(result.bytes))
            {
                failed_ = true;
                lastError_ = "session event channel reported an invalid partial-write length";
                return false;
            }
            remaining -= result.bytes;
            continue;
        }
        failed_ = true;
        lastError_ = result.error.empty() ? "session event channel closed before queued events were delivered"
                                          : result.error;
        return false;
    }
    if (outgoing_.size() == 0)
        inspectionPageGate_.complete();
    return outgoing_.size() == 0;
#endif
}

void EditorSessionService::fail(std::string error, std::uint64_t runtimeSerial) noexcept
{
    if (failed_)
        return;
    failed_ = true;
    stopRequested_ = true;
    lastError_ = std::move(error);
    if (helloAccepted_ && runtimeSerial != 0)
    {
        try
        {
            enqueue_event("error", {{"message", lastError_}}, runtimeSerial);
            flush_events();
        }
        catch (...)
        {
        }
    }
}

void EditorSessionService::close_endpoints() noexcept
{
#if !defined(_WIN32)
    if (mappingAddress_ != nullptr)
    {
        ::munmap(mappingAddress_, mappingBytes_);
        mappingAddress_ = nullptr;
        mappingBytes_ = 0;
    }
    for (const int descriptor : {commandRead_, eventWrite_, mappingFd_})
        if (descriptor >= 0)
            ::close(descriptor);
#endif
    commandRead_ = -1;
    eventWrite_ = -1;
    mappingFd_ = -1;
}
} // namespace platformator
