#include "editor/editorsession.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/flatset.h"
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
#include "scripting/behaviorfactoryregistry.h"

namespace platformator
{
namespace
{
constexpr std::size_t MaximumInspectionObjects = 4096;
constexpr std::size_t MaximumHierarchyDepth = 256;
constexpr std::size_t MaximumHierarchyPageItems = 64;
constexpr std::size_t MaximumIdentifierBytes = 128;
constexpr std::size_t MaximumPathBytes = 256;
constexpr std::size_t MaximumReasonBytes = 256;
constexpr std::size_t MaximumInspectedBehaviors = 8;
constexpr platformator::ScriptInspectionLimits DefaultScriptInspectionLimits{24, MaximumIdentifierBytes, 8, 4096};

using metadata_detail::bounded_utf8_prefix;

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

nlohmann::json handle_json(platformator::ObjectHandle handle, platformator_detail::ObjectKind kind)
{
    return {{"runtime_serial", handle.runtimeSerial}, {"slot", handle.slot},
            {"generation", handle.generation}, {"kind", object_kind_name(kind)}};
}

nlohmann::json safe_number(double value)
{
    return std::isfinite(value) ? nlohmann::json(value) : nlohmann::json(nullptr);
}

nlohmann::json vec2_json(const Eigen::Vector2f &value)
{
    return {{"x", safe_number(value.x())}, {"y", safe_number(value.y())}};
}

nlohmann::json rect_json(const SDL_FRect &rect)
{
    return {{"x", safe_number(rect.x)}, {"y", safe_number(rect.y)},
            {"w", safe_number(rect.w)}, {"h", safe_number(rect.h)}};
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

template <typename EntryT>
bool build_hierarchy_snapshot(platformator::Runtime &runtime,
                              std::vector<EntryT> &snapshot, std::string &error)
{
    const auto &allObjects = runtime.getAllGameObjects();
    if (allObjects.size() > MaximumInspectionObjects)
    {
        error = "runtime hierarchy exceeds the 4096 object inspection bound";
        return false;
    }

    FlatPointerSet<GameObject> liveObjects;
    std::vector<GameObject *> roots;
    roots.reserve(allObjects.size());
    for (GameObject *object : allObjects)
    {
        if (object != nullptr && object->getLifecycle() == ObjectLifecycle::Live)
        {
            if (liveObjects.insert(object) && object->getParent() == nullptr)
                roots.push_back(object);
        }
    }

    std::vector<PendingHierarchyObject> pending;
    pending.reserve(liveObjects.size());
    for (std::size_t index = roots.size(); index > 0; --index)
        pending.push_back({roots[index - 1], {}, false, 0, index - 1});

    snapshot.clear();
    snapshot.reserve(liveObjects.size());
    FlatPointerSet<GameObject> visited;
    while (!pending.empty())
    {
        const auto current = pending.back();
        pending.pop_back();
        GameObject *object = current.object;
        if (object == nullptr || !liveObjects.contains(object) || !visited.insert(object))
        {
            error = "runtime hierarchy contains a stale or repeated child";
            return false;
        }
        if (current.depth > MaximumHierarchyDepth)
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
        std::string name = bounded_utf8_prefix(object->getName(), MaximumIdentifierBytes, nameTruncated);
        snapshot.push_back(EntryT{handle, current.parent, object->getId(), std::move(name), nameTruncated,
                                  object->getActive(), current.hasParent, current.depth, current.siblingIndex});

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

template <typename EntryT>
nlohmann::json hierarchy_entry_json(const EntryT &entry)
{
    nlohmann::json parent = nullptr;
    if (entry.hasParent)
        parent = handle_json(entry.parent, platformator_detail::ObjectKind::GameObject);
    return {{"handle", handle_json(entry.handle, platformator_detail::ObjectKind::GameObject)},
            {"file_id", entry.fileId}, {"name", entry.name},
            {"name_truncated", entry.nameTruncated}, {"active", entry.active},
            {"parent", std::move(parent)}, {"depth", entry.depth},
            {"sibling_index", entry.siblingIndex}};
}

nlohmann::json script_component_snapshot(const ScriptComponent &component)
{
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
            const std::string displayType = bounded_utf8_prefix(typeName, MaximumIdentifierBytes, typeTruncated);
            const auto inspected =
                platformator::inspectRegisteredScriptFields(typeName, *behavior, DefaultScriptInspectionLimits);
            nlohmann::json fields = nlohmann::json::array();
            for (const auto &field : inspected.fields)
            {
                bool nameTruncated = false;
                bool reasonTruncated = false;
                fields.push_back({{"name", bounded_utf8_prefix(field.name, MaximumIdentifierBytes, nameTruncated)},
                                  {"kind", field.kind}, {"available", field.available},
                                  {"truncated", field.truncated || nameTruncated},
                                  {"value", field.value},
                                  {"unavailable_reason",
                                   bounded_utf8_prefix(field.unavailableReason, MaximumReasonBytes, reasonTruncated)}});
            }
            if (!inspected.providerAvailable)
            {
                if (const auto descriptor = BehaviorFactoryRegistry::getInstance().findDescriptor(typeName))
                {
                    for (const auto &declared : descriptor->fields)
                    {
                        bool nameTruncated = false;
                        fields.push_back(
                            {{"name", bounded_utf8_prefix(declared.name, MaximumIdentifierBytes, nameTruncated)},
                             {"kind", script_field_kind_name(declared.kind)},
                             {"available", false},
                             {"truncated", nameTruncated},
                             {"value", nullptr},
                             {"unavailable_reason", "no generated read-only accessor"}});
                    }
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
            behaviors.push_back(
                {{"provider_available", false},
                 {"unavailable_reason", bounded_utf8_prefix(exception.what(), MaximumReasonBytes, reasonTruncated)}});
        }
        catch (...)
        {
            behaviors.push_back({{"provider_available", false},
                                 {"unavailable_reason", "generated field provider failed"}});
        }
    }
    return {{"behaviors", std::move(behaviors)}, {"omitted_behavior_count", instances.size() - count}};
}

nlohmann::json animator_snapshot(const Animator &value)
{
    bool clipNameTruncated = false;
    const std::string clipPath = value.getCurrentClip() ? value.getCurrentClip()->getFilePath() : std::string{};
    return {{"playing", value.getIsPlaying()}, {"paused", value.getIsPaused()},
            {"frame_index", value.getCurrentFrameIndex()},
            {"playback_speed", safe_number(value.getPlaybackSpeed())},
            {"clip_name", bounded_utf8_prefix(clipPath, MaximumIdentifierBytes, clipNameTruncated)},
            {"clip_name_truncated", clipNameTruncated}};
}

nlohmann::json audio_snapshot(const Audio &value)
{
    bool pathTruncated = false;
    return {{"file_path", bounded_utf8_prefix(value.getFilePath(), MaximumPathBytes, pathTruncated)},
            {"path_truncated", pathTruncated}, {"auto_play", value.getAutoPlay()},
            {"gain", safe_number(value.getGain())}, {"loop_count", safe_number(value.getLoopCount())},
            {"playing", value.isPlaying()}, {"paused", value.isPaused()}};
}

nlohmann::json camera_snapshot(const Camera &value)
{
    return {{"width", safe_number(value.getWidth())},
            {"height", safe_number(value.getHeight())},
            {"rect", rect_json(value.getCamera())}};
}

nlohmann::json collider_snapshot(const Collider &value, platformator_detail::ObjectKind &outKind)
{
    nlohmann::json properties = {{"trigger", value.getIsTrigger()},
                                 {"collision_group", value.getCollisionGroup()},
                                 {"collision_mask", value.getCollisionMask()},
                                 {"offset", vec2_json(value.getOffset())}};
    if (value.getColliderType() == ColliderType::BoxCollider)
    {
        const auto &box = static_cast<const BoxCollider &>(value);
        outKind = platformator_detail::ObjectKind::BoxCollider;
        properties["width"] = safe_number(box.getWidthWithoutScale());
        properties["height"] = safe_number(box.getHeightWithoutScale());
    }
    else
    {
        const auto &circle = static_cast<const CircleCollider &>(value);
        outKind = platformator_detail::ObjectKind::CircleCollider;
        properties["radius"] = safe_number(circle.getRadiusWithoutScale());
    }
    return properties;
}

nlohmann::json rigidbody_snapshot(const Rigidbody &value)
{
    return {{"velocity", vec2_json(value.getVelocity())},
            {"force", vec2_json(value.getForce())},
            {"mass", safe_number(value.getMass())},
            {"angular_velocity", safe_number(value.getAngularVelocity())},
            {"torque", safe_number(value.getTorque())},
            {"friction", safe_number(value.getFriction())},
            {"restitution", safe_number(value.getRestitution())},
            {"body_type", static_cast<int>(value.getBodyType())},
            {"gravity", value.getGravity()}, {"sleeping", value.getIsSleeping()}};
}

nlohmann::json sprite_snapshot(const Sprite &value)
{
    bool pathTruncated = false;
    const auto *texture = value.getTextureWrapper();
    nlohmann::json properties = {
        {"width", safe_number(value.getWidthWithoutScale())},
        {"height", safe_number(value.getHeightWithoutScale())},
        {"flip", static_cast<int>(value.getFlip())},
        {"asset_path", texture ? nlohmann::json(bounded_utf8_prefix(texture->getFilePath(), MaximumPathBytes,
                                                                    pathTruncated))
                               : nlohmann::json(nullptr)},
        {"path_truncated", pathTruncated}};
    if (value.hasSourceRect())
    {
        properties["source_rect"] = rect_json(*value.getSourceRect());
    }
    return properties;
}

nlohmann::json component_snapshot(Component &component)
{
    using platformator_detail::ObjectKind;
    ObjectKind kind = ObjectKind::Component;
    nlohmann::json properties = nlohmann::json::object();
    switch (component.getType())
    {
    case ANIMATOR:
        kind = ObjectKind::Animator;
        properties = animator_snapshot(static_cast<const Animator &>(component));
        break;
    case AUDIO:
        kind = ObjectKind::Audio;
        properties = audio_snapshot(static_cast<const Audio &>(component));
        break;
    case CAMERA:
        kind = ObjectKind::Camera;
        properties = camera_snapshot(static_cast<const Camera &>(component));
        break;
    case COLLIDER:
        properties = collider_snapshot(static_cast<const Collider &>(component), kind);
        break;
    case RIGID_BODY:
        kind = ObjectKind::Rigidbody;
        properties = rigidbody_snapshot(static_cast<const Rigidbody &>(component));
        break;
    case SCRIPT:
        kind = ObjectKind::ScriptComponent;
        properties = script_component_snapshot(static_cast<const ScriptComponent &>(component));
        break;
    case SPRITE:
        kind = ObjectKind::Sprite;
        properties = sprite_snapshot(static_cast<const Sprite &>(component));
        break;
    case COMPONENT_TYPE_COUNT:
        break;
    }
    const char *typeName = kind == ObjectKind::Component ? "unknown" : object_kind_name(kind);
    return {{"handle", handle_json(component.getHandle(), kind)}, {"file_id", component.getId()},
            {"type", typeName}, {"properties", std::move(properties)}};
}
} // namespace

void EditorSessionService::reset_hierarchy_snapshot() noexcept
{
    hierarchySnapshot_.clear();
    hierarchySnapshotRevision_ = 0;
    nextHierarchyCursor_ = 0;
}

bool EditorSessionService::enqueue_inspection_event(std::string type, nlohmann::json body,
                                                    std::uint64_t runtimeSerial)
{
    if (enqueue_event(std::move(type), std::move(body), runtimeSerial, true))
        return true;
    fail("could not queue the bounded runtime inspection response", runtimeSerial);
    return false;
}

bool EditorSessionService::enqueue_inspection_response(const char *eventType, std::uint64_t requestSequence,
                                                       std::string status, nlohmann::json details,
                                                       std::uint64_t runtimeSerial)
{
    details["status"] = std::move(status);
    details["request_sequence"] = requestSequence;
    return enqueue_inspection_event(eventType, std::move(details), runtimeSerial);
}

bool EditorSessionService::begin_inspection_request(const editor_session::Message &message,
                                                    std::uint64_t runtimeSerial, const char *responseEvent,
                                                    bool resetSnapshotOnStale, Runtime *&outRuntime, bool &outOk)
{
    outRuntime = nullptr;
    if (!inspectionPageGate_.try_begin())
    {
        outOk = enqueue_inspection_event("inspection_busy",
                                         {{"request_sequence", message.sequence},
                                          {"message", "wait for the outstanding inspection response"}},
                                         runtimeSerial);
        return false;
    }

    auto respond = [&](std::string status, nlohmann::json details = nlohmann::json::object()) {
        return enqueue_inspection_response(responseEvent, message.sequence, std::move(status), std::move(details),
                                           runtimeSerial);
    };
    if (!capabilities_.runtimeInspection)
    {
        outOk = respond("unsupported", {{"message", "runtime inspection is not advertised"}});
        return false;
    }
    const auto serialValue = message.body.find("runtime_serial");
    std::uint64_t requestedRuntimeSerial = 0;
    if (serialValue == message.body.end() || !read_unsigned(*serialValue, requestedRuntimeSerial))
    {
        outOk = respond("invalid", {{"message", "runtime_serial must be an unsigned integer"}});
        return false;
    }
    auto *runtime = platformator::Runtime::tryGetCurrent();
    if (runtime == nullptr || runtime->getRuntimeSerial() != runtimeSerial || requestedRuntimeSerial != runtimeSerial)
    {
        if (resetSnapshotOnStale)
            reset_hierarchy_snapshot();
        outOk = respond("stale", {{"runtime_serial", runtimeSerial},
                                  {"message", "request belongs to another Runtime"}});
        return false;
    }
    outRuntime = runtime;
    outOk = true;
    return true;
}

bool EditorSessionService::handle_list_objects(const editor_session::Message &message, std::uint64_t runtimeSerial)
{
    Runtime *runtime = nullptr;
    bool ok = false;
    if (!begin_inspection_request(message, runtimeSerial, "object_page", true, runtime, ok))
        return ok;

    auto respond = [&](std::string status, nlohmann::json details = nlohmann::json::object()) {
        return enqueue_inspection_response("object_page", message.sequence, std::move(status), std::move(details),
                                           runtimeSerial);
    };

    std::uint64_t cursor = 0;
    std::uint64_t snapshotRevision = 0;
    const auto cursorValue = message.body.find("cursor");
    const auto revisionValue = message.body.find("snapshot_revision");
    const bool hasCursor = cursorValue != message.body.end();
    const bool hasRevision = revisionValue != message.body.end();
    if (hasCursor != hasRevision || (hasCursor && !read_unsigned(*cursorValue, cursor)) ||
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
        std::vector<HierarchySnapshotEntry> newSnapshot;
        std::string snapshotError;
        if (!build_hierarchy_snapshot(*runtime, newSnapshot, snapshotError))
        {
            reset_hierarchy_snapshot();
            return respond("limit_exceeded", {{"message", std::move(snapshotError)}});
        }
        hierarchySnapshot_ = std::move(newSnapshot);
        hierarchySnapshotRevision_ = nextInspectionRevision_;
        nextInspectionRevision_ =
            nextInspectionRevision_ == std::numeric_limits<std::uint64_t>::max() ? 0 : nextInspectionRevision_ + 1;
        nextHierarchyCursor_ = 0;
        snapshotRevision = hierarchySnapshotRevision_;
        cursor = 0;
    }
    else
    {
        if (snapshotRevision == 0 || snapshotRevision != hierarchySnapshotRevision_ || cursor != nextHierarchyCursor_)
        {
            return respond("stale", {{"snapshot_revision", snapshotRevision},
                                      {"message", "snapshot continuation is expired or out of order"}});
        }
        std::vector<HierarchySnapshotEntry> currentSnapshot;
        std::string snapshotError;
        if (!build_hierarchy_snapshot(*runtime, currentSnapshot, snapshotError) ||
            currentSnapshot != hierarchySnapshot_)
        {
            reset_hierarchy_snapshot();
            return respond("stale", {{"snapshot_revision", snapshotRevision},
                                      {"message", snapshotError.empty() ? "hierarchy changed during pagination"
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
        objects.push_back(hierarchy_entry_json(hierarchySnapshot_[index]));
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
        reset_hierarchy_snapshot();
    return true;
}

bool EditorSessionService::handle_inspect_object(const editor_session::Message &message, std::uint64_t runtimeSerial)
{
    Runtime *runtime = nullptr;
    bool ok = false;
    if (!begin_inspection_request(message, runtimeSerial, "object_snapshot", false, runtime, ok))
        return ok;

    auto respond = [&](std::string status, nlohmann::json details = nlohmann::json::object()) {
        return enqueue_inspection_response("object_snapshot", message.sequence, std::move(status), std::move(details),
                                           runtimeSerial);
    };
    const auto handleValue = message.body.find("handle");
    std::uint64_t slot = 0;
    std::uint64_t generation = 0;
    if (handleValue == message.body.end() || !handleValue->is_object() ||
        !handleValue->contains("slot") || !read_unsigned((*handleValue)["slot"], slot) ||
        slot > std::numeric_limits<std::uint32_t>::max() ||
        !handleValue->contains("generation") ||
        !read_unsigned((*handleValue)["generation"], generation) || generation == 0 ||
        !handleValue->contains("kind") || !(*handleValue)["kind"].is_string() ||
        (*handleValue)["kind"].get<std::string>() != "game_object")
        return respond("invalid", {{"message", "inspect_object requires a game_object handle"}});

    const platformator::ObjectHandle handle{static_cast<std::uint32_t>(slot), generation, runtimeSerial};
    auto *base = runtime->resolveHandle(handle, platformator_detail::ObjectKind::GameObject);
    auto *object = base == nullptr ? nullptr : static_cast<GameObject *>(base);
    if (object == nullptr || object->getLifecycle() != ObjectLifecycle::Live)
        return respond("stale", {{"runtime_serial", runtimeSerial},
                                  {"handle", handle_json(handle, platformator_detail::ObjectKind::GameObject)},
                                  {"message", "GameObject handle is stale or retired"}});

    const auto position = object->getPosition();
    const auto &scale = object->getScale();
    bool nameTruncated = false;
    bool tagTruncated = false;
    nlohmann::json parent = nullptr;
    if (GameObject *parentObject = object->getParent();
        parentObject != nullptr && parentObject->getLifecycle() == ObjectLifecycle::Live)
        parent = handle_json(parentObject->getHandle(), platformator_detail::ObjectKind::GameObject);

    nlohmann::json children = nlohmann::json::array();
    const auto &objectChildren = object->getChildren();
    const std::size_t childCount = std::min(objectChildren.size(), MaximumHierarchyPageItems);
    for (std::size_t index = 0; index < childCount; ++index)
    {
        GameObject *child = objectChildren[index];
        if (child != nullptr && child->getLifecycle() == ObjectLifecycle::Live)
            children.push_back(handle_json(child->getHandle(), platformator_detail::ObjectKind::GameObject));
    }
    nlohmann::json components = nlohmann::json::array();
    Component *const *objectComponents = object->getComponents();
    for (std::size_t index = 0; index < COMPONENT_TYPE_COUNT; ++index)
        if (objectComponents[index] != nullptr && objectComponents[index]->getLifecycle() == ObjectLifecycle::Live)
            components.push_back(component_snapshot(*objectComponents[index]));

    nlohmann::json details = nlohmann::json::object();
    details["runtime_serial"] = runtimeSerial;
    details["handle"] = handle_json(handle, platformator_detail::ObjectKind::GameObject);
    details["file_id"] = object->getId();
    details["name"] = bounded_utf8_prefix(object->getName(), MaximumIdentifierBytes, nameTruncated);
    details["name_truncated"] = nameTruncated;
    details["tag"] = bounded_utf8_prefix(object->getTag(), MaximumIdentifierBytes, tagTruncated);
    details["tag_truncated"] = tagTruncated;
    details["active"] = object->getActive();
    details["position"] = vec2_json(position);
    details["scale"] = vec2_json(scale);
    details["rotation"] = safe_number(object->getRotation());
    details["parent"] = std::move(parent);
    details["children"] = std::move(children);
    details["omitted_child_count"] = objectChildren.size() - childCount;
    details["components"] = std::move(components);
    return respond("ok", std::move(details));
}
} // namespace platformator
