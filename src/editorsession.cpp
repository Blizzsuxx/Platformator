#include "editorsession.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>

#include "platformator/animator.h"
#include "platformator/audio.h"
#include "platformator/boxcollider.h"
#include "platformator/camera.h"
#include "platformator/circlecollider.h"
#include "platformator/gameobject.h"
#include "platformator/rigidbody.h"
#include "platformator/runtime.h"
#include "platformator/scriptcomponent.h"
#include "platformator/scriptregistration.h"
#include "platformator/sprite.h"
#include "sdlwindow.h"

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
constexpr auto MinimumCaptureInterval = std::chrono::microseconds(66'667);
constexpr std::size_t ReservedFrameMessages = 8;
constexpr std::size_t ReservedFrameBytes = 16U * 1024U;
constexpr std::size_t MaximumInspectionObjects = 4096;
constexpr std::size_t MaximumHierarchyPageItems = 64;
constexpr std::size_t MaximumHierarchyNameBytes = 128;
constexpr std::size_t MaximumInspectedBehaviors = 8;

std::string bounded_utf8(std::string_view text, std::size_t byteLimit, bool &truncated)
{
    std::string result;
    result.reserve(std::min(text.size(), byteLimit));
    constexpr std::string_view replacement = "\xef\xbf\xbd";
    std::size_t offset = 0;
    while (offset < text.size())
    {
        const auto first = static_cast<unsigned char>(text[offset]);
        std::size_t width = 1;
        if (first >= 0xc2 && first <= 0xdf)
            width = 2;
        else if (first >= 0xe0 && first <= 0xef)
            width = 3;
        else if (first >= 0xf0 && first <= 0xf4)
            width = 4;
        bool valid = first <= 0x7f || (first >= 0xc2 && first <= 0xf4);
        valid = valid && offset + width <= text.size();
        for (std::size_t index = 1; valid && index < width; ++index)
        {
            const auto continuation = static_cast<unsigned char>(text[offset + index]);
            valid = continuation >= 0x80 && continuation <= 0xbf;
        }
        if (valid && width == 3)
        {
            const auto second = static_cast<unsigned char>(text[offset + 1]);
            valid = !((first == 0xe0 && second < 0xa0) || (first == 0xed && second >= 0xa0));
        }
        if (valid && width == 4)
        {
            const auto second = static_cast<unsigned char>(text[offset + 1]);
            valid = !((first == 0xf0 && second < 0x90) || (first == 0xf4 && second >= 0x90));
        }
        const auto appendWidth = valid ? width : replacement.size();
        if (appendWidth > byteLimit - result.size())
        {
            truncated = true;
            break;
        }
        if (valid)
            result.append(text.substr(offset, width));
        else
            result.append(replacement);
        offset += valid ? width : 1;
    }
    truncated = truncated || offset < text.size();
    return result;
}

const char *object_kind_name(platformator_detail::ObjectKind kind) noexcept
{
    using platformator_detail::ObjectKind;
    switch (kind)
    {
    case ObjectKind::GameObject: return "game_object";
    case ObjectKind::Animator: return "animator";
    case ObjectKind::Audio: return "audio";
    case ObjectKind::Camera: return "camera";
    case ObjectKind::Collider: return "collider";
    case ObjectKind::BoxCollider: return "box_collider";
    case ObjectKind::CircleCollider: return "circle_collider";
    case ObjectKind::Rigidbody: return "rigidbody";
    case ObjectKind::ScriptComponent: return "script_component";
    case ObjectKind::Sprite: return "sprite";
    case ObjectKind::Component: return "component";
    case ObjectKind::BaseObject: return "base_object";
    }
    return "unknown";
}

nlohmann::json handle_json(platformator::ObjectHandle handle,
                           platformator_detail::ObjectKind kind)
{
    return {{"runtime_serial", handle.runtimeSerial}, {"slot", handle.slot},
            {"generation", handle.generation}, {"kind", object_kind_name(kind)}};
}

nlohmann::json safe_number(double value)
{
    return std::isfinite(value) ? nlohmann::json(value) : nlohmann::json(nullptr);
}

bool read_unsigned(const nlohmann::json &value, std::uint64_t &result)
{
    if (value.is_number_unsigned())
    {
        result = value.get<std::uint64_t>();
        return true;
    }
    if (!value.is_number_integer())
        return false;
    const auto signedValue = value.get<std::int64_t>();
    if (signedValue < 0)
        return false;
    result = static_cast<std::uint64_t>(signedValue);
    return true;
}

struct PendingHierarchyObject
{
    GameObject *object = nullptr;
    platformator::ObjectHandle parent{};
    bool hasParent = false;
    std::size_t depth = 0;
    std::size_t siblingIndex = 0;
};

bool build_hierarchy_snapshot(platformator::Runtime &runtime,
                              std::vector<nlohmann::json> &snapshot, std::string &error)
{
    const auto &allObjects = runtime.getAllGameObjects();
    if (allObjects.size() > MaximumInspectionObjects)
    {
        error = "runtime hierarchy exceeds the 4096 object inspection bound";
        return false;
    }

    std::unordered_set<GameObject *> liveObjects;
    liveObjects.reserve(allObjects.size());
    for (GameObject *object : allObjects)
        if (object != nullptr && object->getLifecycle() == ObjectLifecycle::Live &&
            !object->getIsMarkedForDeletion())
            liveObjects.insert(object);

    std::vector<PendingHierarchyObject> pending;
    pending.reserve(liveObjects.size());
    std::vector<GameObject *> roots;
    roots.reserve(liveObjects.size());
    for (GameObject *object : allObjects)
        if (object != nullptr && liveObjects.contains(object) && object->getParent() == nullptr)
            roots.push_back(object);
    for (std::size_t index = roots.size(); index > 0; --index)
    {
        pending.push_back({roots[index - 1], {}, false, 0, index - 1});
    }

    snapshot.clear();
    snapshot.reserve(liveObjects.size());
    std::unordered_set<GameObject *> visited;
    visited.reserve(liveObjects.size());
    while (!pending.empty())
    {
        const auto current = pending.back();
        pending.pop_back();
        GameObject *object = current.object;
        if (object == nullptr || !liveObjects.contains(object) || !visited.insert(object).second)
        {
            error = "runtime hierarchy contains a stale or repeated child";
            return false;
        }
        if (current.depth > 256)
        {
            error = "runtime hierarchy exceeds the 256 level inspection bound";
            return false;
        }
        const auto handle = object->getHandle();
        if (!handle || handle.runtimeSerial != runtime.getRuntimeSerial())
        {
            error = "runtime hierarchy contains an object without a current handle";
            return false;
        }
        bool nameTruncated = false;
        const std::string name = bounded_utf8(object->getName(), MaximumHierarchyNameBytes,
                                              nameTruncated);
        nlohmann::json parent = nullptr;
        if (current.hasParent)
            parent = handle_json(current.parent, platformator_detail::ObjectKind::GameObject);
        snapshot.push_back({{"handle", handle_json(handle, platformator_detail::ObjectKind::GameObject)},
                            {"file_id", object->getId()}, {"name", name},
                            {"name_truncated", nameTruncated}, {"active", object->getActive()},
                            {"parent", std::move(parent)}, {"depth", current.depth},
                            {"sibling_index", current.siblingIndex}});

        const auto &children = object->getChildren();
        for (std::size_t index = children.size(); index > 0; --index)
        {
            GameObject *child = children[index - 1];
            if (child != nullptr && liveObjects.contains(child))
                pending.push_back({child, handle, true, current.depth + 1, index - 1});
        }
    }
    if (visited.size() != liveObjects.size())
    {
        error = "runtime hierarchy contains an unparented or unreachable object";
        return false;
    }
    return true;
}

nlohmann::json script_component_snapshot(const ScriptComponent &component)
{
    constexpr platformator::ScriptInspectionLimits limits{24, 128, 8, 4096};
    nlohmann::json behaviors = nlohmann::json::array();
    const auto &instances = component.getBehaviors();
    const std::size_t count = std::min(instances.size(), MaximumInspectedBehaviors);
    for (std::size_t index = 0; index < count; ++index)
    {
        Behavior *behavior = instances[index];
        if (behavior == nullptr)
        {
            behaviors.push_back({{"provider_available", false},
                                 {"unavailable_reason", "behavior instance is unavailable"}});
            continue;
        }
        try
        {
            const std::string typeName = behavior->getTypeName();
            bool typeTruncated = false;
            const std::string displayType = bounded_utf8(typeName, 128, typeTruncated);
            const auto inspected = platformator::inspectRegisteredScriptFields(typeName, *behavior, limits);
            nlohmann::json fields = nlohmann::json::array();
            for (const auto &field : inspected.fields)
            {
                bool nameTruncated = false;
                bool reasonTruncated = false;
                fields.push_back({{"name", bounded_utf8(field.name, 128, nameTruncated)},
                                  {"kind", field.kind}, {"available", field.available},
                                  {"truncated", field.truncated || nameTruncated},
                                  {"value", field.value},
                                  {"unavailable_reason", bounded_utf8(field.unavailableReason, 256,
                                                                       reasonTruncated)}});
            }
            if (!inspected.providerAvailable)
            {
                const auto catalog = platformator::exportRegisteredBehaviorCatalog();
                for (const auto &descriptor : catalog.value("behaviors", nlohmann::json::array()))
                {
                    if (!descriptor.is_object() || descriptor.value("name", std::string{}) != typeName)
                        continue;
                    const auto declaredFields = descriptor.find("fields");
                    if (declaredFields == descriptor.end() || !declaredFields->is_array())
                        break;
                    for (const auto &declared : *declaredFields)
                    {
                        if (!declared.is_object())
                            continue;
                        bool nameTruncated = false;
                        fields.push_back({{"name", bounded_utf8(declared.value("name", std::string{}),
                                                                  128, nameTruncated)},
                                          {"kind", declared.value("kind", std::string("opaque_json"))},
                                          {"available", false}, {"truncated", nameTruncated},
                                          {"value", nullptr},
                                          {"unavailable_reason", "no generated read-only accessor"}});
                    }
                    break;
                }
            }
            behaviors.push_back({{"type", displayType}, {"type_truncated", typeTruncated},
                                 {"provider_available", inspected.providerAvailable},
                                 {"output_truncated", inspected.outputTruncated},
                                 {"omitted_field_count", inspected.omittedFieldCount},
                                 {"fields", std::move(fields)}});
        }
        catch (const std::exception &exception)
        {
            bool reasonTruncated = false;
            behaviors.push_back({{"provider_available", false},
                                 {"unavailable_reason", bounded_utf8(exception.what(), 256,
                                                                      reasonTruncated)}});
        }
        catch (...)
        {
            behaviors.push_back({{"provider_available", false},
                                 {"unavailable_reason", "generated field provider failed"}});
        }
    }
    return {{"behaviors", std::move(behaviors)}, {"omitted_behavior_count", instances.size() - count}};
}

nlohmann::json component_snapshot(Component &component)
{
    using platformator_detail::ObjectKind;
    ObjectKind kind = ObjectKind::Component;
    const char *typeName = "unknown";
    nlohmann::json properties = nlohmann::json::object();
    switch (component.getType())
    {
    case ANIMATOR:
    {
        auto &value = static_cast<Animator &>(component);
        kind = ObjectKind::Animator;
        typeName = "animator";
        bool clipNameTruncated = false;
        properties = {{"playing", value.getIsPlaying()}, {"paused", value.getIsPaused()},
                      {"frame_index", value.getCurrentFrameIndex()},
                      {"playback_speed", safe_number(value.getPlaybackSpeed())},
                      {"clip_name", bounded_utf8(value.getCurrentClipName(), 128, clipNameTruncated)},
                      {"clip_name_truncated", clipNameTruncated}};
        break;
    }
    case AUDIO:
    {
        auto &value = static_cast<Audio &>(component);
        kind = ObjectKind::Audio;
        typeName = "audio";
        bool pathTruncated = false;
        properties = {{"file_path", bounded_utf8(value.getFilePath(), 256, pathTruncated)},
                      {"path_truncated", pathTruncated}, {"auto_play", value.getAutoPlay()},
                      {"gain", safe_number(value.getGain())},
                      {"loop_count", safe_number(value.getLoopCount())},
                      {"playing", value.isPlaying()}, {"paused", value.isPaused()}};
        break;
    }
    case CAMERA:
    {
        auto &value = static_cast<Camera &>(component);
        kind = ObjectKind::Camera;
        typeName = "camera";
        const auto rect = value.getCamera();
        properties = {{"width", safe_number(value.getWidth())},
                      {"height", safe_number(value.getHeight())},
                      {"rect", {{"x", safe_number(rect.x)}, {"y", safe_number(rect.y)},
                                 {"w", safe_number(rect.w)}, {"h", safe_number(rect.h)}}}};
        break;
    }
    case COLLIDER:
    {
        auto &value = static_cast<Collider &>(component);
        const auto offset = value.getOffset();
        properties = {{"trigger", value.getIsTrigger()}, {"collision_group", value.getCollisionGroup()},
                      {"collision_mask", value.getCollisionMask()},
                      {"offset", {{"x", safe_number(offset.x())}, {"y", safe_number(offset.y())}}}};
        if (value.getColliderType() == ColliderType::BoxCollider)
        {
            auto &box = static_cast<BoxCollider &>(component);
            kind = ObjectKind::BoxCollider;
            typeName = "box_collider";
            properties["width"] = safe_number(box.getWidthWithoutScale());
            properties["height"] = safe_number(box.getHeightWithoutScale());
        }
        else
        {
            auto &circle = static_cast<CircleCollider &>(component);
            kind = ObjectKind::CircleCollider;
            typeName = "circle_collider";
            properties["radius"] = safe_number(circle.getRadiusWithoutScale());
        }
        break;
    }
    case RIGID_BODY:
    {
        auto &value = static_cast<Rigidbody &>(component);
        kind = ObjectKind::Rigidbody;
        typeName = "rigidbody";
        const auto velocity = value.getVelocity();
        const auto force = value.getForce();
        properties = {{"velocity", {{"x", safe_number(velocity.x())}, {"y", safe_number(velocity.y())}}},
                      {"force", {{"x", safe_number(force.x())}, {"y", safe_number(force.y())}}},
                      {"mass", safe_number(value.getMass())},
                      {"angular_velocity", safe_number(value.getAngularVelocity())},
                      {"torque", safe_number(value.getTorque())},
                      {"friction", safe_number(value.getFriction())},
                      {"restitution", safe_number(value.getRestitution())},
                      {"body_type", static_cast<int>(value.getBodyType())},
                      {"gravity", value.getGravity()}, {"sleeping", value.getIsSleeping()}};
        break;
    }
    case SCRIPT:
    {
        auto &value = static_cast<ScriptComponent &>(component);
        kind = ObjectKind::ScriptComponent;
        typeName = "script_component";
        properties = script_component_snapshot(value);
        break;
    }
    case SPRITE:
    {
        auto &value = static_cast<Sprite &>(component);
        kind = ObjectKind::Sprite;
        typeName = "sprite";
        bool pathTruncated = false;
        const auto *texture = value.getTextureWrapper();
        properties = {{"width", safe_number(value.getWidthWithoutScale())},
                      {"height", safe_number(value.getHeightWithoutScale())},
                      {"flip", static_cast<int>(value.getFlip())},
                      {"asset_path", texture ? nlohmann::json(bounded_utf8(texture->getFilePath(),
                                                        256, pathTruncated)) : nlohmann::json(nullptr)},
                      {"path_truncated", pathTruncated}};
        if (value.hasSourceRect())
        {
            const auto *source = value.getSourceRect();
            properties["source_rect"] = {{"x", safe_number(source->x)}, {"y", safe_number(source->y)},
                                          {"w", safe_number(source->w)}, {"h", safe_number(source->h)}};
        }
        break;
    }
    case COMPONENT_TYPE_COUNT:
        break;
    }
    return {{"handle", handle_json(component.getHandle(), kind)}, {"file_id", component.getId()},
            {"type", typeName}, {"properties", std::move(properties)}};
}

#if !defined(_WIN32)
std::string system_error(const char *operation, int error)
{
    return std::string(operation) + ": " + std::strerror(error);
}

bool validate_pipe_descriptor(int descriptor, int requiredAccess, const char *label,
                              std::string &error)
{
    const int descriptorFlags = ::fcntl(descriptor, F_GETFD);
    const int statusFlags = ::fcntl(descriptor, F_GETFL);
    struct stat status{};
    if (descriptorFlags < 0 || statusFlags < 0 || ::fstat(descriptor, &status) != 0)
    {
        error = system_error(label, errno);
        return false;
    }
    if (!S_ISFIFO(status.st_mode) || (statusFlags & O_ACCMODE) != requiredAccess ||
        (statusFlags & O_NONBLOCK) == 0)
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
        if (mappingDescriptorFlags < 0 || mappingStatusFlags < 0 ||
            ::fstat(mappingFd_, &mappingStatus) != 0)
            throw std::invalid_argument(system_error("cannot inspect session frame mapping", errno));
        if ((mappingStatusFlags & O_ACCMODE) != O_RDWR || !S_ISREG(mappingStatus.st_mode) ||
            mappingStatus.st_size < 0 ||
            static_cast<std::uint64_t>(mappingStatus.st_size) != FrameMappingBytes)
            throw std::invalid_argument("frame mapping descriptor must name exactly two 640x360 RGBA8 slots");
        if (::fcntl(mappingFd_, F_SETFD, mappingDescriptorFlags | FD_CLOEXEC) < 0)
            throw std::runtime_error(system_error("cannot make frame mapping close-on-exec", errno));
        mappingAddress_ = ::mmap(nullptr, FrameMappingBytes, PROT_READ | PROT_WRITE, MAP_SHARED,
                                 mappingFd_, 0);
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
            auto request = std::move(pendingRequests_.front());
            pendingRequests_.pop_front();
            std::string encodedError;
            const auto encoded = editor_session::encode_frame(
                request, editor_session::Direction::Request, encodedError);
            if (!encodedError.empty())
            {
                fail("buffered session request could not be accounted: " + encodedError, runtimeSerial);
                break;
            }
            pendingRequestBytes_ -= std::min(pendingRequestBytes_, encoded.size());
            ++handled;
            if (!accept_request(std::move(request), runtimeSerial, window))
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
            const auto decoded = incoming_.feed(std::span<const std::uint8_t>(
                reinterpret_cast<const std::uint8_t *>(buffer.data()), transferred));
            if (!decoded.succeeded())
            {
                fail(decoded.error, runtimeSerial);
                break;
            }
            for (const auto &request : decoded.messages)
            {
                std::string encodedError;
                const auto encoded = editor_session::encode_frame(
                    request, editor_session::Direction::Request, encodedError);
                if (!encodedError.empty() || encoded.size() >
                        editor_session::MaximumQueuedBytes -
                            std::min(pendingRequestBytes_, editor_session::MaximumQueuedBytes))
                {
                    fail(encodedError.empty() ? "decoded request backlog exceeded its byte bound"
                                              : "decoded request could not be accounted: " + encodedError,
                         runtimeSerial);
                    break;
                }
                pendingRequestBytes_ += encoded.size();
                pendingRequests_.push_back(request);
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
                           nlohmann::json{{"request_sequence", requestSequence},
                                          {"step_number", stepNumber}},
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

bool EditorSessionService::accept_request(editor_session::Message message,
                                         std::uint64_t runtimeSerial,
                                         ::SDLWindow *window)
{
    const auto enqueue_inspection_event = [&](std::string type, nlohmann::json body) {
        if (enqueue_event(std::move(type), std::move(body), runtimeSerial, true))
            return true;
        fail("could not queue the bounded runtime inspection response", runtimeSerial);
        return false;
    };

    if (!helloAccepted_)
    {
        if (message.type != "hello" || message.sequence != 1 ||
            !requestSequences_.accept(message.sequence))
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
            fail(capabilities_.error.empty()
                     ? "this runner does not expose a compatible editor-session protocol"
                     : capabilities_.error,
                 runtimeSerial);
            return false;
        }
        if (window != nullptr)
        {
            window->enableEditorSessionControls();
            if (capabilities_.frameStream)
            {
                window->setEditorSessionFrameCapture(
                    [this, runtimeSerial](::SDL_Renderer *renderer, int width, int height) {
                        capture_frame(runtimeSerial, renderer, width, height);
                    });
            }
        }
        const auto capabilities = catalog.find("editor_capabilities");
        const nlohmann::json capabilityBody = capabilities == catalog.end()
            ? nlohmann::json::object() : *capabilities;
        if (!enqueue_event("capabilities", capabilityBody, runtimeSerial) ||
            !enqueue_event("ready", nlohmann::json::object(), runtimeSerial))
        {
            fail("could not queue initial session capability and ready events", runtimeSerial);
            return false;
        }
        return true;
    }

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
    if (message.type == "pause")
    {
        if (!capabilities_.playControl || window == nullptr || window->isEditorSessionPaused() ||
            !window->setEditorSessionPaused(true))
        {
            fail("Pause is unavailable or invalid in the current session state", runtimeSerial);
            return false;
        }
        if (!enqueue_event("state",
                           nlohmann::json{{"state", "paused"},
                                          {"request_sequence", message.sequence}},
                           runtimeSerial, true))
        {
            fail("could not acknowledge the accepted Pause request", runtimeSerial);
            return false;
        }
        return true;
    }
    if (message.type == "resume")
    {
        if (!capabilities_.playControl || window == nullptr || !window->isEditorSessionPaused() ||
            !window->setEditorSessionPaused(false))
        {
            fail("Resume is unavailable or invalid in the current session state", runtimeSerial);
            return false;
        }
        if (!enqueue_event("state",
                           nlohmann::json{{"state", "running"},
                                          {"request_sequence", message.sequence}},
                           runtimeSerial, true))
        {
            fail("could not acknowledge the accepted Resume request", runtimeSerial);
            return false;
        }
        return true;
    }
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
    if (message.type == "list_objects")
    {
        if (!inspectionPageGate_.try_begin())
        {
            if (!enqueue_inspection_event("inspection_busy",
                    {{"request_sequence", message.sequence},
                     {"message", "wait for the outstanding inspection response"}}))
                return false;
            return true;
        }

        auto respond = [&](std::string status, nlohmann::json details = nlohmann::json::object()) {
            details["status"] = std::move(status);
            details["request_sequence"] = message.sequence;
            return enqueue_inspection_event("object_page", std::move(details));
        };
        if (!capabilities_.runtimeInspection)
            return respond("unsupported", {{"message", "runtime inspection is not advertised"}});
        const auto serialValue = message.body.find("runtime_serial");
        std::uint64_t requestedRuntimeSerial = 0;
        if (serialValue == message.body.end() || !read_unsigned(*serialValue, requestedRuntimeSerial))
            return respond("invalid", {{"message", "runtime_serial must be an unsigned integer"}});
        auto *runtime = platformator::Runtime::tryGetCurrent();
        if (runtime == nullptr || runtime->getRuntimeSerial() != runtimeSerial ||
            requestedRuntimeSerial != runtimeSerial)
        {
            hierarchySnapshot_.clear();
            hierarchySnapshotRevision_ = 0;
            nextHierarchyCursor_ = 0;
            return respond("stale", {{"runtime_serial", runtimeSerial},
                                      {"message", "request belongs to another Runtime"}});
        }

        std::uint64_t cursor = 0;
        std::uint64_t snapshotRevision = 0;
        const auto cursorValue = message.body.find("cursor");
        const auto revisionValue = message.body.find("snapshot_revision");
        const bool hasCursor = cursorValue != message.body.end();
        const bool hasRevision = revisionValue != message.body.end();
        if (hasCursor != hasRevision ||
            (hasCursor && !read_unsigned(*cursorValue, cursor)) ||
            (hasRevision && !read_unsigned(*revisionValue, snapshotRevision)))
            return respond("invalid", {{"message", "continuations require unsigned cursor and snapshot_revision"}});

        std::size_t pageSize = MaximumHierarchyPageItems;
        if (const auto pageSizeValue = message.body.find("page_size"); pageSizeValue != message.body.end())
        {
            std::uint64_t requestedPageSize = 0;
            if (!read_unsigned(*pageSizeValue, requestedPageSize) || requestedPageSize == 0 ||
                requestedPageSize > MaximumHierarchyPageItems)
                return respond("invalid", {{"message", "page_size must be between 1 and 64"}});
            pageSize = static_cast<std::size_t>(requestedPageSize);
        }

        if (!hasCursor)
        {
            if (nextInspectionRevision_ == 0)
                return respond("unavailable", {{"message", "inspection revision space is exhausted"}});
            std::vector<nlohmann::json> newSnapshot;
            std::string snapshotError;
            if (!build_hierarchy_snapshot(*runtime, newSnapshot, snapshotError))
            {
                hierarchySnapshot_.clear();
                hierarchySnapshotRevision_ = 0;
                nextHierarchyCursor_ = 0;
                return respond("limit_exceeded", {{"message", std::move(snapshotError)}});
            }
            hierarchySnapshot_ = std::move(newSnapshot);
            hierarchySnapshotRevision_ = nextInspectionRevision_;
            nextInspectionRevision_ = nextInspectionRevision_ ==
                    std::numeric_limits<std::uint64_t>::max()
                ? 0 : nextInspectionRevision_ + 1;
            nextHierarchyCursor_ = 0;
            snapshotRevision = hierarchySnapshotRevision_;
            cursor = 0;
        }
        else
        {
            if (snapshotRevision == 0 || snapshotRevision != hierarchySnapshotRevision_ ||
                cursor != nextHierarchyCursor_)
            {
                return respond("stale", {{"snapshot_revision", snapshotRevision},
                                          {"message", "snapshot continuation is expired or out of order"}});
            }
            std::vector<nlohmann::json> currentSnapshot;
            std::string snapshotError;
            if (!build_hierarchy_snapshot(*runtime, currentSnapshot, snapshotError) ||
                currentSnapshot != hierarchySnapshot_)
            {
                hierarchySnapshot_.clear();
                hierarchySnapshotRevision_ = 0;
                nextHierarchyCursor_ = 0;
                return respond("stale", {{"snapshot_revision", snapshotRevision},
                                          {"message", snapshotError.empty()
                                              ? "hierarchy changed during pagination"
                                              : std::move(snapshotError)}});
            }
        }

        if (cursor > hierarchySnapshot_.size())
            return respond("invalid", {{"snapshot_revision", snapshotRevision},
                                        {"message", "cursor is beyond the snapshot"}});
        const std::size_t start = static_cast<std::size_t>(cursor);
        const std::size_t finish = std::min(start + pageSize, hierarchySnapshot_.size());
        nlohmann::json objects = nlohmann::json::array();
        for (std::size_t index = start; index < finish; ++index)
            objects.push_back(hierarchySnapshot_[index]);
        const bool hasMore = finish < hierarchySnapshot_.size();
        nlohmann::json nextCursor = hasMore ? nlohmann::json(finish) : nlohmann::json(nullptr);
        const bool enqueued = respond("ok", {{"runtime_serial", runtimeSerial},
                                              {"snapshot_revision", snapshotRevision},
                                              {"cursor", start}, {"next_cursor", nextCursor},
                                              {"objects", std::move(objects)}});
        if (!enqueued)
            return false;
        if (hasMore)
            nextHierarchyCursor_ = finish;
        else
        {
            hierarchySnapshot_.clear();
            hierarchySnapshotRevision_ = 0;
            nextHierarchyCursor_ = 0;
        }
        return true;
    }
    if (message.type == "inspect_object")
    {
        if (!inspectionPageGate_.try_begin())
        {
            if (!enqueue_inspection_event("inspection_busy",
                    {{"request_sequence", message.sequence},
                     {"message", "wait for the outstanding inspection response"}}))
                return false;
            return true;
        }

        auto respond = [&](std::string status, nlohmann::json details = nlohmann::json::object()) {
            details["status"] = std::move(status);
            details["request_sequence"] = message.sequence;
            return enqueue_inspection_event("object_snapshot", std::move(details));
        };
        if (!capabilities_.runtimeInspection)
            return respond("unsupported", {{"message", "runtime inspection is not advertised"}});
        const auto serialValue = message.body.find("runtime_serial");
        const auto handleValue = message.body.find("handle");
        std::uint64_t requestedRuntimeSerial = 0;
        std::uint64_t slot = 0;
        std::uint64_t generation = 0;
        if (serialValue == message.body.end() || !read_unsigned(*serialValue, requestedRuntimeSerial) ||
            handleValue == message.body.end() || !handleValue->is_object() ||
            !handleValue->contains("slot") || !read_unsigned((*handleValue)["slot"], slot) ||
            slot > std::numeric_limits<std::uint32_t>::max() ||
            !handleValue->contains("generation") ||
            !read_unsigned((*handleValue)["generation"], generation) || generation == 0 ||
            !handleValue->contains("kind") || !(*handleValue)["kind"].is_string() ||
            (*handleValue)["kind"].get<std::string>() != "game_object")
            return respond("invalid", {{"message", "inspect_object requires a game_object handle"}});
        auto *runtime = platformator::Runtime::tryGetCurrent();
        if (runtime == nullptr || runtime->getRuntimeSerial() != runtimeSerial ||
            requestedRuntimeSerial != runtimeSerial)
            return respond("stale", {{"runtime_serial", runtimeSerial},
                                      {"message", "request belongs to another Runtime"}});

        const platformator::ObjectHandle handle{static_cast<std::uint32_t>(slot), generation,
                                                requestedRuntimeSerial};
        auto *base = runtime->resolveHandle(handle, platformator_detail::ObjectKind::GameObject);
        auto *object = base == nullptr ? nullptr : static_cast<GameObject *>(base);
        if (object == nullptr || object->getLifecycle() != ObjectLifecycle::Live ||
            object->getIsMarkedForDeletion())
            return respond("stale", {{"runtime_serial", runtimeSerial},
                                      {"handle", handle_json(handle,
                                          platformator_detail::ObjectKind::GameObject)},
                                      {"message", "GameObject handle is stale or retired"}});

        const auto position = object->getPosition();
        const auto &scale = object->getScale();
        bool nameTruncated = false;
        bool tagTruncated = false;
        nlohmann::json parent = nullptr;
        if (GameObject *parentObject = object->getParent(); parentObject != nullptr &&
            parentObject->getLifecycle() == ObjectLifecycle::Live)
            parent = handle_json(parentObject->getHandle(), platformator_detail::ObjectKind::GameObject);

        nlohmann::json children = nlohmann::json::array();
        const auto &objectChildren = object->getChildren();
        const std::size_t childCount = std::min(objectChildren.size(), MaximumHierarchyPageItems);
        for (std::size_t index = 0; index < childCount; ++index)
        {
            GameObject *child = objectChildren[index];
            if (child != nullptr && child->getLifecycle() == ObjectLifecycle::Live)
                children.push_back(handle_json(child->getHandle(),
                                               platformator_detail::ObjectKind::GameObject));
        }
        nlohmann::json components = nlohmann::json::array();
        Component *const *objectComponents = object->getComponents();
        for (std::size_t index = 0; index < COMPONENT_TYPE_COUNT; ++index)
            if (objectComponents[index] != nullptr &&
                objectComponents[index]->getLifecycle() == ObjectLifecycle::Live)
                components.push_back(component_snapshot(*objectComponents[index]));

        nlohmann::json details = nlohmann::json::object();
        details["runtime_serial"] = runtimeSerial;
        details["handle"] = handle_json(handle, platformator_detail::ObjectKind::GameObject);
        details["file_id"] = object->getId();
        details["name"] = bounded_utf8(object->getName(), MaximumHierarchyNameBytes, nameTruncated);
        details["name_truncated"] = nameTruncated;
        details["tag"] = bounded_utf8(object->getTag(), MaximumHierarchyNameBytes, tagTruncated);
        details["tag_truncated"] = tagTruncated;
        details["active"] = object->getActive();
        details["position"] = {{"x", safe_number(position.x())},
                                {"y", safe_number(position.y())}};
        details["scale"] = {{"x", safe_number(scale.x())}, {"y", safe_number(scale.y())}};
        details["rotation"] = safe_number(object->getRotation());
        details["parent"] = std::move(parent);
        details["children"] = std::move(children);
        details["omitted_child_count"] = objectChildren.size() - childCount;
        details["components"] = std::move(components);
        return respond("ok", std::move(details));
    }
    fail("unsupported editor-session command: " + message.type, runtimeSerial);
    return false;
}

void EditorSessionService::capture_frame(std::uint64_t runtimeSerial, ::SDL_Renderer *renderer,
                                        int width, int height) noexcept
{
    if (!captureVisible_ || !capabilities_.frameStream || failed_ || stopRequested_ ||
        renderer == nullptr)
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
    frameSlotBusy_[slotIndex] = true;

    int outputWidth = 0;
    int outputHeight = 0;
    if (width != static_cast<int>(FrameWidth) || height != static_cast<int>(FrameHeight) ||
        !SDL_GetRenderOutputSize(renderer, &outputWidth, &outputHeight) ||
        outputWidth != static_cast<int>(FrameWidth) || outputHeight != static_cast<int>(FrameHeight))
    {
        frameSlotBusy_[slotIndex] = false;
        fail("session renderer output is not exactly 640x360", runtimeSerial);
        return;
    }
    if (mappingAddress_ == nullptr || mappingBytes_ != FrameMappingBytes || nextFrameSequence_ == 0)
    {
        frameSlotBusy_[slotIndex] = false;
        fail("session frame mapping or sequence is unavailable", runtimeSerial);
        return;
    }

    SDL_Surface *readback = SDL_RenderReadPixels(renderer, nullptr);
    if (readback == nullptr)
    {
        frameSlotBusy_[slotIndex] = false;
        fail(std::string("session frame readback failed: ") + SDL_GetError(), runtimeSerial);
        return;
    }
    SDL_Surface *rgba = SDL_ConvertSurface(readback, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(readback);
    if (rgba == nullptr || rgba->w != static_cast<int>(FrameWidth) ||
        rgba->h != static_cast<int>(FrameHeight) || rgba->pitch < static_cast<int>(FrameStride) ||
        rgba->pixels == nullptr)
    {
        if (rgba != nullptr)
            SDL_DestroySurface(rgba);
        frameSlotBusy_[slotIndex] = false;
        fail("session readback could not be converted to bounded RGBA8 pixels", runtimeSerial);
        return;
    }
    if (!SDL_LockSurface(rgba))
    {
        SDL_DestroySurface(rgba);
        frameSlotBusy_[slotIndex] = false;
        fail(std::string("session RGBA readback could not be locked: ") + SDL_GetError(),
             runtimeSerial);
        return;
    }

    auto *destination = static_cast<std::byte *>(mappingAddress_) + slotIndex * FrameSlotBytes;
    const auto *source = static_cast<const std::byte *>(rgba->pixels);
    for (std::size_t row = 0; row < FrameHeight; ++row)
        std::copy_n(source + row * static_cast<std::size_t>(rgba->pitch), FrameStride,
                    destination + row * FrameStride);
    SDL_UnlockSurface(rgba);
    SDL_DestroySurface(rgba);
    std::atomic_thread_fence(std::memory_order_release);

    const std::uint64_t frameSequence = nextFrameSequence_;
    const bool queued = enqueue_event(
        "frame_ready",
        nlohmann::json{{"slot", slotIndex}, {"frame_sequence", frameSequence},
                       {"width", FrameWidth}, {"height", FrameHeight},
                       {"stride", FrameStride}, {"format", "rgba8"}},
        runtimeSerial, false);
    if (!queued)
    {
        frameSlotBusy_[slotIndex] = false;
        return;
    }
    frameSlotSequences_[slotIndex] = frameSequence;
    nextFrameSequence_ = frameSequence == std::numeric_limits<std::uint64_t>::max()
        ? 0 : frameSequence + 1;
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
    nextEventSequence_ = nextEventSequence_ == std::numeric_limits<std::uint64_t>::max()
        ? 0 : nextEventSequence_ + 1;
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
        const auto result = write_without_sigpipe(eventWrite_,
                                                  bytes.first(std::min(remaining, bytes.size())));
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
        lastError_ = result.error.empty()
            ? "session event channel closed before queued events were delivered" : result.error;
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
