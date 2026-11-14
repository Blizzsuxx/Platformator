#include "validation.h"

#include "camerainterpretation.h"
#include "editorwork.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <unordered_set>
#include <vector>

#include <SDL3/SDL.h>
#include <json.hpp>
#include <oneapi/tbb/task_group.h>
#include <platformator/collider.h>
#include <platformator/gameobject.h>
#include <platformator/rigidbody.h>

namespace platformator_editor
{
namespace
{
using Json = nlohmann::json;

void add_issue(ValidationResult &result, ValidationSeverity severity, int objectId,
               std::optional<int> componentId, std::string fieldPath, std::string message,
               bool blocksRun)
{
    result.issues.push_back({severity, objectId, componentId, std::move(fieldPath), std::move(message), blocksRun});
    result.blocksRun |= blocksRun;
}

std::optional<double> numeric_value(const Json *value)
{
    if (value == nullptr || !value->is_number())
        return std::nullopt;
    try
    {
        return value->get<double>();
    }
    catch (...)
    {
        return std::nullopt;
    }
}

std::optional<std::int64_t> integer_value(const Json *value)
{
    if (value == nullptr || (!value->is_number_integer() && !value->is_number_unsigned()))
        return std::nullopt;
    try
    {
        if (value->is_number_unsigned())
        {
            const auto parsed = value->get<std::uint64_t>();
            if (parsed > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
                return std::nullopt;
            return static_cast<std::int64_t>(parsed);
        }
        return value->get<std::int64_t>();
    }
    catch (...)
    {
        return std::nullopt;
    }
}

const Json *member(const Json &object, const char *key)
{
    if (!object.is_object())
        return nullptr;
    const auto found = object.find(key);
    return found == object.end() ? nullptr : &*found;
}

void require_number(const Json &owner, const char *key, std::string path, int objectId,
                    std::optional<int> componentId, ValidationResult &result,
                    bool positive = false, bool nonnegative = false,
                    bool floatRepresentable = false)
{
    const auto value = numeric_value(member(owner, key));
    if (!value)
    {
        add_issue(result, ValidationSeverity::Error, objectId, componentId, std::move(path),
                  "A finite numeric value is required.", true);
        return;
    }
    if (!std::isfinite(*value))
    {
        add_issue(result, ValidationSeverity::Error, objectId, componentId, std::move(path),
                  "The value must be finite.", true);
        return;
    }
    if (floatRepresentable && std::abs(*value) > std::numeric_limits<float>::max())
    {
        add_issue(result, ValidationSeverity::Error, objectId, componentId, std::move(path),
                  "The value is outside the finite range supported by the engine.", true);
        return;
    }
    if (positive && *value <= 0.0)
        add_issue(result, ValidationSeverity::Error, objectId, componentId, std::move(path),
                  "The value must be greater than zero.", true);
    else if (nonnegative && *value < 0.0)
        add_issue(result, ValidationSeverity::Error, objectId, componentId, std::move(path),
                  "The value must not be negative.", true);
}

void require_boolean(const Json &owner, const char *key, std::string path, int objectId,
                     std::optional<int> componentId, ValidationResult &result)
{
    const auto *value = member(owner, key);
    if (value == nullptr || !value->is_boolean())
        add_issue(result, ValidationSeverity::Error, objectId, componentId, std::move(path),
                  "A boolean value is required.", true);
}

void require_vector(const Json &owner, const char *key, std::string path, int objectId,
                    std::optional<int> componentId, ValidationResult &result)
{
    const auto *value = member(owner, key);
    if (value == nullptr || !value->is_object())
    {
        add_issue(result, ValidationSeverity::Error, objectId, componentId, std::move(path),
                  "A two-dimensional vector is required.", true);
        return;
    }
    require_number(*value, "x", path + ".x", objectId, componentId, result);
    require_number(*value, "y", path + ".y", objectId, componentId, result);
}

bool asset_path_is_beneath(const std::filesystem::path &root, const std::filesystem::path &path)
{
    auto rootPart = root.begin();
    auto pathPart = path.begin();
    for (; rootPart != root.end(); ++rootPart, ++pathPart)
    {
        if (pathPart == path.end() || *rootPart != *pathPart)
            return false;
    }
    return true;
}

std::filesystem::path resolve_asset_path(const DocumentSession &document, std::string_view rawPath,
                                        std::string &error,
                                        std::filesystem::path *logicalPath = nullptr)
{
    std::string normalized(rawPath);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    const std::filesystem::path path(normalized);
    if (document.assetRoot.empty() || path.is_absolute() || path.empty() ||
        *path.begin() != "assets" || path.filename().empty())
    {
        error = "Asset paths must be relative assets/... paths.";
        return {};
    }
    for (const auto &part : path)
    {
        if (part == "..")
        {
            error = "Asset paths cannot leave the configured asset root.";
            return {};
        }
    }
    std::error_code filesystemError;
    const auto canonicalRoot = std::filesystem::weakly_canonical(document.assetRoot, filesystemError);
    if (filesystemError)
    {
        error = "The configured asset root cannot be resolved.";
        return {};
    }
    const auto relative = path.lexically_relative("assets");
    if (logicalPath != nullptr)
        *logicalPath = (document.assetRoot / relative).lexically_normal();
    const auto candidate = std::filesystem::weakly_canonical(canonicalRoot / relative, filesystemError);
    if (filesystemError || !asset_path_is_beneath(canonicalRoot, candidate))
    {
        error = "Asset path resolves outside the configured asset root.";
        return {};
    }
    return candidate;
}

void validate_asset_reference(const DocumentSession &document, const Json &component,
                              const char *field, std::string fieldPath, int objectId,
                              int componentId, ResourceKind kind, ValidationResult &result)
{
    const auto *value = member(component, field);
    if (value == nullptr || !value->is_string())
    {
        add_issue(result, ValidationSeverity::Error, objectId, componentId, std::move(fieldPath),
                  "An asset path string is required; use an empty string when no asset is assigned.", true);
        return;
    }
    const auto &reference = value->get_ref<const std::string &>();
    // The runtime serializers deliberately skip asset loading for empty
    // Sprite, Animator and Audio references. Empty is a valid optional value;
    // a missing/non-string field or a non-empty unresolved path is not.
    if (reference.empty())
        return;
    std::string resolutionError;
    std::filesystem::path logicalPath;
    const auto asset = resolve_asset_path(document, reference, resolutionError, &logicalPath);
    if (asset.empty())
    {
        add_issue(result, ValidationSeverity::Error, objectId, componentId, std::move(fieldPath),
                  std::move(resolutionError), true);
        return;
    }
    result.assetDependencies.push_back(
        {std::move(logicalPath), kind, objectId, componentId});
    std::error_code filesystemError;
    if (!std::filesystem::is_regular_file(asset, filesystemError) || filesystemError)
        add_issue(result, ValidationSeverity::Error, objectId, componentId, std::move(fieldPath),
                  "The referenced asset is missing or is not a file.", true);
}

void validate_camera(const Json &component, int objectId, int componentId, ValidationResult &result)
{
    const auto dimensions = interpret_camera_dimensions(component);
    auto validate_dimension = [&](const std::optional<double> &value, bool conflict,
                                  bool malformed, const char *name) {
        if (conflict)
        {
            add_issue(result, ValidationSeverity::Error, objectId, componentId, name,
                      std::string("Conflicting canonical and legacy camera ") + name +
                          " values require an explicit edit.", true);
            return;
        }
        if (malformed || !value)
        {
            add_issue(result, ValidationSeverity::Error, objectId, componentId, name,
                      malformed ? "Camera dimensions must be finite numbers."
                                : "A camera dimension is required.", true);
            return;
        }
        if (*value <= 0.0)
            add_issue(result, ValidationSeverity::Error, objectId, componentId, name,
                      "Camera dimensions must be finite and positive.", true);
    };
    validate_dimension(dimensions.width, dimensions.widthConflict,
                       dimensions.widthMalformed, "width");
    validate_dimension(dimensions.height, dimensions.heightConflict,
                       dimensions.heightMalformed, "height");
}

void validate_collider(const Json &component, int objectId, int componentId, ValidationResult &result)
{
    const auto kind = integer_value(member(component, "colliderType"));
    if (!kind || (*kind != static_cast<int>(ColliderType::CircleCollider) &&
                  *kind != static_cast<int>(ColliderType::BoxCollider)))
    {
        add_issue(result, ValidationSeverity::Error, objectId, componentId, "colliderType",
                  "The collider shape is not supported by the engine.", true);
        return;
    }
    if (*kind == static_cast<int>(ColliderType::BoxCollider))
    {
        require_number(component, "width", "width", objectId, componentId, result, true, false, true);
        require_number(component, "height", "height", objectId, componentId, result, true, false, true);
    }
    else
        require_number(component, "radius", "radius", objectId, componentId, result, true, false, true);
    require_vector(component, "offset", "offset", objectId, componentId, result);
    require_boolean(component, "trigger", "trigger", objectId, componentId, result);

    for (const char *field : {"collisionGroup", "collisionMask"})
    {
        const auto *value = member(component, field);
        bool valid = value != nullptr && (value->is_number_integer() || value->is_number_unsigned());
        if (valid && value->is_number_integer())
            valid = integer_value(value).value_or(-1) >= 0;
        std::uint64_t mask = 0;
        if (valid)
        {
            if (value->is_number_unsigned())
                mask = value->get<std::uint64_t>();
            else
                mask = static_cast<std::uint64_t>(value->get<std::int64_t>());
            valid = mask <= std::numeric_limits<std::uint32_t>::max();
            if (valid && std::string_view(field) == "collisionGroup")
                valid = mask != 0 && (mask & (mask - 1)) == 0;
        }
        if (!valid)
            add_issue(result, ValidationSeverity::Error, objectId, componentId, field,
                      std::string(field) == "collisionGroup"
                          ? "The collider active layer must be one nonzero 32-bit layer bit."
                          : "The collider collision mask must be a nonnegative 32-bit value.",
                      true);
    }
}

void validate_body(const Json &component, int objectId, int componentId, ValidationResult &result)
{
    require_vector(component, "velocity", "velocity", objectId, componentId, result);
    require_vector(component, "force", "force", objectId, componentId, result);
    const auto bodyType = integer_value(member(component, "bodyType"));
    if (!bodyType || *bodyType < DYNAMIC || *bodyType > KINEMATIC)
        add_issue(result, ValidationSeverity::Error, objectId, componentId, "bodyType",
                  "The rigid body type is not supported by the engine.", true);
    require_number(component, "mass", "mass", objectId, componentId, result, false, true);
    if (bodyType && *bodyType == DYNAMIC)
    {
        const auto mass = numeric_value(member(component, "mass"));
        if (mass && std::isfinite(*mass) && *mass <= 0.0)
            add_issue(result, ValidationSeverity::Error, objectId, componentId, "mass",
                      "Dynamic bodies require positive mass.", true);
    }
    require_number(component, "angularVelocity", "angularVelocity", objectId, componentId, result);
    require_number(component, "torque", "torque", objectId, componentId, result);
    require_number(component, "friction", "friction", objectId, componentId, result, false, true);
    const auto restitution = numeric_value(member(component, "restitution"));
    if (!restitution || !std::isfinite(*restitution) || *restitution < 0.0 || *restitution > 1.0)
        add_issue(result, ValidationSeverity::Error, objectId, componentId, "restitution",
                  "Restitution must be between zero and one.", true);
    require_boolean(component, "gravity", "gravity", objectId, componentId, result);
}

void validate_sprite(const DocumentSession &document, const Json &component,
                     int objectId, int componentId, ValidationResult &result)
{
    validate_asset_reference(document, component, "textureFilePath", "textureFilePath",
                             objectId, componentId, ResourceKind::Texture, result);
    const auto flip = integer_value(member(component, "flip"));
    if (!flip || *flip < 0 || *flip > 3)
        add_issue(result, ValidationSeverity::Error, objectId, componentId, "flip",
                  "The sprite flip mask is not a supported SDL flip value.", true);
    for (const char *field : {"width", "height"})
    {
        const auto value = numeric_value(member(component, field));
        if (!value || !std::isfinite(*value) || *value <= 0.0)
            add_issue(result, ValidationSeverity::Warning, objectId, componentId, field,
                      "The sprite has no positive finite render extent.", false);
    }
    const auto *enabled = member(component, "sourceRectEnabled");
    if (enabled == nullptr || !enabled->is_boolean())
        add_issue(result, ValidationSeverity::Error, objectId, componentId, "sourceRectEnabled",
                  "A source rectangle enable flag is required.", true);
    else if (enabled->get<bool>())
    {
        const auto *rect = member(component, "sourceRect");
        if (rect == nullptr || !rect->is_object())
            add_issue(result, ValidationSeverity::Error, objectId, componentId, "sourceRect",
                      "An enabled source rectangle must contain x, y, width and height.", true);
        else
        {
            for (const char *field : {"x", "y", "w", "h"})
                require_number(*rect, field, std::string("sourceRect.") + field, objectId, componentId, result);
            const auto width = numeric_value(member(*rect, "w"));
            const auto height = numeric_value(member(*rect, "h"));
            if ((width && std::isfinite(*width) && *width <= 0.0) ||
                (height && std::isfinite(*height) && *height <= 0.0))
                add_issue(result, ValidationSeverity::Warning, objectId, componentId, "sourceRect",
                          "The source rectangle has no positive area.", false);
        }
    }
}

void validate_animator(const DocumentSession &document, const Json &component,
                       int objectId, int componentId, ValidationResult &result)
{
    validate_asset_reference(document, component, "animationClipFilePath", "animationClipFilePath",
                             objectId, componentId, ResourceKind::Animation, result);
    const auto frame = integer_value(member(component, "currentFrameIndex"));
    if (!frame || *frame < 0)
        add_issue(result, ValidationSeverity::Error, objectId, componentId, "currentFrameIndex",
                  "The current animation frame index must be a nonnegative integer.", true);
    const auto speed = numeric_value(member(component, "playbackSpeed"));
    if (!speed || !std::isfinite(*speed))
        add_issue(result, ValidationSeverity::Error, objectId, componentId, "playbackSpeed",
                  "Animation playback speed must be finite.", true);
    else if (*speed < 0.0)
        add_issue(result, ValidationSeverity::Warning, objectId, componentId, "playbackSpeed",
                  "The runtime clamps negative animation speed to zero; the saved value is preserved.", false);
    require_boolean(component, "playing", "playing", objectId, componentId, result);
}

void validate_audio(const DocumentSession &document, const Json &component,
                    int objectId, int componentId, ValidationResult &result)
{
    validate_asset_reference(document, component, "filePath", "filePath", objectId, componentId,
                             ResourceKind::Audio, result);
    const auto gain = numeric_value(member(component, "gain"));
    if (!gain || !std::isfinite(*gain))
        add_issue(result, ValidationSeverity::Error, objectId, componentId, "gain",
                  "Audio gain must be finite.", true);
    else if (*gain < 0.0)
        add_issue(result, ValidationSeverity::Warning, objectId, componentId, "gain",
                  "The runtime clamps negative audio gain to zero; the saved value is preserved.", false);
    const auto loopCount = numeric_value(member(component, "loopCount"));
    if (!loopCount || !std::isfinite(*loopCount) ||
        *loopCount < static_cast<double>(std::numeric_limits<int>::min()) ||
        *loopCount > static_cast<double>(std::numeric_limits<int>::max()))
        add_issue(result, ValidationSeverity::Error, objectId, componentId, "loopCount",
                  "Audio loop count must be a finite integer-range value.", true);
    require_boolean(component, "autoPlay", "autoPlay", objectId, componentId, result);
}

const Json *find_behavior_descriptor(const Json &catalog, std::string_view name)
{
    const auto behaviors = catalog.find("behaviors");
    if (behaviors == catalog.end() || !behaviors->is_array())
        return nullptr;
    for (const auto &behavior : *behaviors)
        if (behavior.is_object() && behavior.value("name", std::string{}) == name)
            return &behavior;
    return nullptr;
}

bool behavior_value_matches(const Json &value, const Json &descriptor, std::string &reason)
{
    reason.clear();
    if (!descriptor.is_object() || !descriptor.contains("kind") || !descriptor["kind"].is_string())
    {
        reason = "behavior field descriptor is malformed";
        return false;
    }
    const std::string kind = descriptor["kind"].get<std::string>();
    if (kind == "optional")
    {
        if (value.is_null())
            return true;
        if (!descriptor.contains("element"))
        {
            reason = "optional field schema is missing its element type";
            return false;
        }
        return behavior_value_matches(value, descriptor["element"], reason);
    }
    if (kind == "array")
    {
        if (!value.is_array() || !descriptor.contains("element"))
        {
            reason = "expected an array matching the behavior field schema";
            return false;
        }
        for (const auto &entry : value)
            if (!behavior_value_matches(entry, descriptor["element"], reason))
            {
                reason = "array element: " + reason;
                return false;
            }
        return true;
    }
    if (kind == "boolean")
    {
        if (value.is_boolean())
            return true;
        reason = "expected a boolean value";
        return false;
    }
    if (kind == "signed_integer")
    {
        if (value.is_number_integer())
            return true;
        reason = "expected a signed integer value";
        return false;
    }
    if (kind == "unsigned_integer")
    {
        if (value.is_number_unsigned() ||
            (value.is_number_integer() && value.get<std::int64_t>() >= 0))
            return true;
        reason = "expected a non-negative integer value";
        return false;
    }
    if (kind == "number")
    {
        if (value.is_number() && std::isfinite(value.get<double>()))
            return true;
        reason = "expected a finite number value";
        return false;
    }
    if (kind == "string")
    {
        if (value.is_string())
            return true;
        reason = "expected a string value";
        return false;
    }
    if (kind == "asset_reference")
    {
        if (value.is_null() && descriptor.value("nullable", false))
            return true;
        if (value.is_string())
            return true;
        reason = descriptor.value("nullable", false)
            ? "expected a string asset reference or null"
            : "expected a string asset reference";
        return false;
    }
    if (kind == "enum")
    {
        if (value.is_string())
        {
            if (!descriptor.contains("enum_values") || !descriptor["enum_values"].is_array())
                return true;
            const bool found = std::any_of(descriptor["enum_values"].begin(), descriptor["enum_values"].end(),
                [&](const Json &entry) { return entry.value("name", std::string{}) == value.get<std::string>(); });
            if (!found)
                reason = "value is not a declared enum option";
            return found;
        }
        if (value.is_number_integer())
            return true;
        reason = "expected an enum option or integer value";
        return false;
    }
    if (kind == "vector2")
    {
        if (value.is_object() && numeric_value(member(value, "x")).has_value() &&
            numeric_value(member(value, "y")).has_value())
            return true;
        reason = "expected an object with numeric x and y values";
        return false;
    }
    if (kind == "rectangle")
    {
        if (value.is_object() && numeric_value(member(value, "x")).has_value() &&
            numeric_value(member(value, "y")).has_value() &&
            numeric_value(member(value, "w")).has_value() &&
            numeric_value(member(value, "h")).has_value())
            return true;
        reason = "expected an object with numeric x, y, w and h values";
        return false;
    }
    if (kind == "object_reference")
    {
        if (value.is_null() && descriptor.value("nullable", false))
            return true;
        if (value.is_number_integer() && value.get<std::int64_t>() >= 0)
            return true;
        reason = descriptor.value("nullable", false)
            ? "expected a non-negative object ID or null"
            : "expected a non-negative object ID";
        return false;
    }
    if (kind == "opaque_json") return true;
    reason = "unsupported behavior field kind '" + kind + "'";
    return false;
}

void validate_script(const Json &component, int objectId, int componentId,
                     const Json &behaviorCatalog, ValidationResult &result)
{
    const Json *behaviors = member(component, "behaviors");
    if (behaviors == nullptr || !behaviors->is_array())
    {
        add_issue(result, ValidationSeverity::Error, objectId, componentId, "behaviors",
                  "A Script component must contain a behaviors array.", true);
        return;
    }
    for (std::size_t index = 0; index < behaviors->size(); ++index)
    {
        const Json &record = (*behaviors)[index];
        const std::string prefix = "behaviors[" + std::to_string(index) + "]";
        if (!record.is_object() || !record.contains("type") || !record["type"].is_string() ||
            record["type"].get_ref<const std::string &>().empty())
        {
            add_issue(result, ValidationSeverity::Error, objectId, componentId, prefix + ".type",
                      "A behavior record must have a nonempty string type name.", true);
            continue;
        }
        const std::string behaviorName = record["type"].get<std::string>();
        const Json *descriptor = find_behavior_descriptor(behaviorCatalog, behaviorName);
        if (descriptor == nullptr || !descriptor->value("field_schema_available", false) ||
            !descriptor->contains("fields") || !(*descriptor)["fields"].is_array())
        {
            add_issue(result, ValidationSeverity::Warning, objectId, componentId, prefix + ".type",
                      "Behavior source schema is unavailable; saved fields were preserved.", false);
            continue;
        }
        if (descriptor->contains("scanner_warning") && (*descriptor)["scanner_warning"].is_string())
            add_issue(result, ValidationSeverity::Warning, objectId, componentId, prefix,
                      (*descriptor)["scanner_warning"].get<std::string>(), false);
        for (const auto &field : (*descriptor)["fields"])
        {
            if (!field.is_object() || !field.contains("name") || !field["name"].is_string())
                continue;
            const std::string fieldName = field["name"].get<std::string>();
            const auto value = record.find(fieldName);
            if (value == record.end())
                continue;
            std::string reason;
            if (!behavior_value_matches(*value, field, reason))
                add_issue(result, ValidationSeverity::Error, objectId, componentId,
                          prefix + "." + fieldName,
                          "Saved value does not match the behavior field schema: " + reason, true);
        }
    }
}

void validate_component(const DocumentSession &document, const Json &component,
                        int objectId, const Json &behaviorCatalog, ValidationResult &result)
{
    const int componentId = component.at("id").get<int>();
    const auto type = integer_value(member(component, "type"));
    if (!type)
    {
        add_issue(result, ValidationSeverity::Error, objectId, componentId, "type",
                  "The component type must be an integer discriminator.", true);
        return;
    }
    switch (*type)
    {
    case ANIMATOR:
        validate_animator(document, component, objectId, componentId, result);
        break;
    case AUDIO:
        validate_audio(document, component, objectId, componentId, result);
        break;
    case CAMERA:
        validate_camera(component, objectId, componentId, result);
        break;
    case COLLIDER:
        validate_collider(component, objectId, componentId, result);
        break;
    case RIGID_BODY:
        validate_body(component, objectId, componentId, result);
        break;
    case SCRIPT:
        validate_script(component, objectId, componentId, behaviorCatalog, result);
        break;
    case SPRITE:
        validate_sprite(document, component, objectId, componentId, result);
        break;
    default:
        add_issue(result, ValidationSeverity::Error, objectId, componentId, "type",
                  "The runtime does not recognize this component type; its serialized data is preserved.", true);
        break;
    }
}

void validate_object_fields(const DocumentSession &document, const Json &object,
                            const Json &behaviorCatalog, ValidationResult &result)
{
    const int objectId = object.at("id").get<int>();
    require_number(object, "rotation", "rotation", objectId, std::nullopt, result);
    require_vector(object, "position", "position", objectId, std::nullopt, result);
    const auto *scale = member(object, "scale");
    if (scale == nullptr || !scale->is_object())
    {
        add_issue(result, ValidationSeverity::Error, objectId, std::nullopt, "scale",
                  "A two-dimensional scale is required.", true);
    }
    else
    {
        require_number(*scale, "x", "scale.x", objectId, std::nullopt, result, true);
        require_number(*scale, "y", "scale.y", objectId, std::nullopt, result, true);
    }

    const auto *components = member(object, "components");
    if (components != nullptr && components->is_array())
        for (const auto &component : *components)
            validate_component(document, component, objectId, behaviorCatalog, result);
}

constexpr std::size_t MaximumLocalInputNodes = 16'384;
constexpr std::size_t MaximumLocalInputBytes = 1024U * 1024U;
constexpr std::size_t MaximumLocalInputDepth = 32;

bool bounded_json_copy(const Json &source, Json &destination, std::size_t depth,
                       std::size_t &nodes, std::size_t &bytes)
{
    if (depth > MaximumLocalInputDepth || ++nodes > MaximumLocalInputNodes)
        return false;
    if (source.is_string())
    {
        const auto &value = source.get_ref<const std::string &>();
        if (value.size() > MaximumLocalInputBytes - bytes)
            return false;
        bytes += value.size();
        destination = value;
        return true;
    }
    if (source.is_object())
    {
        destination = Json::object();
        for (auto item = source.begin(); item != source.end(); ++item)
        {
            if (item.key().size() > MaximumLocalInputBytes - bytes)
                return false;
            bytes += item.key().size();
            Json child;
            if (!bounded_json_copy(item.value(), child, depth + 1, nodes, bytes))
                return false;
            destination[item.key()] = std::move(child);
        }
        return true;
    }
    if (source.is_array())
    {
        destination = Json::array();
        for (const auto &value : source)
        {
            Json child;
            if (!bounded_json_copy(value, child, depth + 1, nodes, bytes))
                return false;
            destination.push_back(std::move(child));
        }
        return true;
    }
    // JSON scalar copies have constant storage and do not traverse scene data.
    destination = source;
    return true;
}

bool copy_selected_member(const Json &source, Json &destination, const char *key,
                          std::size_t &nodes, std::size_t &bytes)
{
    const auto *value = member(source, key);
    if (value == nullptr)
        return true;
    Json copied;
    if (!bounded_json_copy(*value, copied, 0, nodes, bytes))
        return false;
    destination[key] = std::move(copied);
    return true;
}

bool copy_validation_component(const Json &source, Json &destination,
                               std::size_t &nodes, std::size_t &bytes)
{
    destination = Json::object();
    if (!copy_selected_member(source, destination, "id", nodes, bytes) ||
        !copy_selected_member(source, destination, "type", nodes, bytes))
        return false;
    const auto type = integer_value(member(source, "type"));
    const char *fields[10]{};
    std::size_t fieldCount = 0;
    if (type == ANIMATOR)
    {
        const char *selected[] = {"animationClipFilePath", "currentFrameIndex", "playbackSpeed", "playing"};
        std::copy(std::begin(selected), std::end(selected), fields);
        fieldCount = std::size(selected);
    }
    else if (type == AUDIO)
    {
        const char *selected[] = {"filePath", "gain", "loopCount", "autoPlay"};
        std::copy(std::begin(selected), std::end(selected), fields);
        fieldCount = std::size(selected);
    }
    else if (type == CAMERA)
    {
        const char *selected[] = {"width", "height", "camera"};
        std::copy(std::begin(selected), std::end(selected), fields);
        fieldCount = std::size(selected);
    }
    else if (type == COLLIDER)
    {
        const char *selected[] = {"colliderType", "width", "height", "radius", "offset",
                                  "trigger", "collisionGroup", "collisionMask"};
        std::copy(std::begin(selected), std::end(selected), fields);
        fieldCount = std::size(selected);
    }
    else if (type == RIGID_BODY)
    {
        const char *selected[] = {"velocity", "force", "bodyType", "mass", "angularVelocity",
                                  "torque", "friction", "restitution", "gravity"};
        std::copy(std::begin(selected), std::end(selected), fields);
        fieldCount = std::size(selected);
    }
    else if (type == SPRITE)
    {
        const char *selected[] = {"textureFilePath", "flip", "width", "height",
                                  "sourceRectEnabled", "sourceRect"};
        std::copy(std::begin(selected), std::end(selected), fields);
        fieldCount = std::size(selected);
    }
    else if (type == SCRIPT)
    {
        fields[0] = "behaviors";
        fieldCount = 1;
    }
    for (std::size_t index = 0; index < fieldCount; ++index)
        if (!copy_selected_member(source, destination, fields[index], nodes, bytes))
            return false;
    return true;
}

bool make_local_validation_input(const DocumentSession &document, int objectId, Json &output)
{
    const auto location = document.indexes.objectsById.find(objectId);
    if (location == document.indexes.objectsById.end())
        return false;
    const Json *source = &document.root;
    bool first = true;
    for (const std::size_t index : location->second.childIndices)
    {
        if (!first)
        {
            const auto *children = member(*source, "children");
            if (children == nullptr)
                return false;
            source = children;
        }
        first = false;
        if (!source->is_array() || index >= source->size())
            return false;
        source = &(*source)[index];
    }
    if (!source->is_object())
        return false;

    std::size_t nodes = 0;
    std::size_t bytes = 0;
    output = Json::object();
    for (const char *field : {"id", "rotation", "position", "scale"})
        if (!copy_selected_member(*source, output, field, nodes, bytes))
            return false;
    const auto *components = member(*source, "components");
    if (components == nullptr || !components->is_array())
        return false;
    Json projectedComponents = Json::array();
    for (const auto &component : *components)
    {
        if (!component.is_object())
            return false;
        Json projected;
        if (!copy_validation_component(component, projected, nodes, bytes))
            return false;
        projectedComponents.push_back(std::move(projected));
    }
    output["components"] = std::move(projectedComponents);
    return true;
}

bool validation_snapshot_exceeds_limits(const DocumentSession &document)
{
    constexpr std::size_t maximumObjects = 100'000;
    constexpr std::size_t maximumComponents = 500'000;
    constexpr std::size_t maximumJsonNodes = 1'000'000;
    constexpr std::size_t maximumEstimatedBytes = 64U * 1024U * 1024U;
    if (document.indexes.objectsById.size() > maximumObjects ||
        document.indexes.componentsById.size() > maximumComponents)
        return true;

    std::vector<const Json *> pending;
    pending.push_back(&document.root);
    std::size_t nodes = 0;
    std::size_t estimatedBytes = 0;
    const auto account = [&](std::size_t bytes) {
        if (bytes > maximumEstimatedBytes - estimatedBytes)
            return false;
        estimatedBytes += bytes;
        return true;
    };
    while (!pending.empty())
    {
        const Json *value = pending.back();
        pending.pop_back();
        if (++nodes > maximumJsonNodes || !account(sizeof(Json)))
            return true;
        if (value->is_string())
        {
            if (!account(value->get_ref<const std::string &>().size()))
                return true;
        }
        else if (value->is_array())
        {
            if (value->size() > maximumJsonNodes - std::min(maximumJsonNodes, nodes + pending.size()))
                return true;
            for (const auto &child : *value)
                pending.push_back(&child);
        }
        else if (value->is_object())
        {
            if (value->size() > maximumJsonNodes - std::min(maximumJsonNodes, nodes + pending.size()))
                return true;
            for (auto iterator = value->begin(); iterator != value->end(); ++iterator)
            {
                if (!account(iterator.key().size()))
                    return true;
                pending.push_back(&iterator.value());
            }
        }
    }
    return false;
}

void bind_result_identity(ValidationCompletion &completion)
{
    completion.result.document = completion.document;
    completion.result.structureRevision = completion.structureRevision;
    completion.result.valueRevision = completion.valueRevision;
    completion.result.requestId = completion.requestId;
}

} // namespace

ValidationResult validate_document(const DocumentSession &document, const Json &behaviorCatalog)
{
    ValidationResult result;
    const auto structuralCheck = parse_document(document.root, document.documentGeneration,
                                                document.sourcePath, document.projectRoot,
                                                document.assetRoot);
    if (!structuralCheck)
    {
        result.structurallyValid = false;
        add_issue(result, ValidationSeverity::Error, -1, std::nullopt, "$",
                  "Document structure is invalid: " + structuralCheck.error, true);
        return result;
    }

    struct Pending
    {
        const Json *object = nullptr;
    };
    std::vector<Pending> pending;
    pending.reserve(document.root.size());
    for (auto iter = document.root.rbegin(); iter != document.root.rend(); ++iter)
        pending.push_back({&*iter});

    while (!pending.empty())
    {
        const Json &object = *pending.back().object;
        pending.pop_back();
        validate_object_fields(document, object, behaviorCatalog, result);
        for (auto iter = object.at("children").rbegin(); iter != object.at("children").rend(); ++iter)
            pending.push_back({&*iter});
    }
    return result;
}

std::vector<int> validation_object_ids_for_resource_consumers(
    const std::vector<ResourceConsumer> &consumers, const DocumentIdentity &document)
{
    std::vector<int> objectIds;
    for (const auto &consumer : consumers)
    {
        if (consumer.document != document || consumer.role != "validation.asset" ||
            std::find(objectIds.begin(), objectIds.end(), consumer.objectId) != objectIds.end())
            continue;
        objectIds.push_back(consumer.objectId);
    }
    return objectIds;
}

struct ValidationService::Impl
{
    struct LocalObject
    {
        int objectId = 0;
        Json input;
    };

    struct Job
    {
        bool incremental = false;
        ValidationCompletion completion;
        DocumentSession snapshot;
        std::shared_ptr<const ValidationResult> baseResult;
        std::uint64_t baseRequestId = 0;
        std::vector<int> replacedObjectIds;
        std::vector<LocalObject> localObjects;
        std::vector<int> inputFailures;
        Json behaviorCatalog = Json::object();
    };

    mutable std::mutex mutex;
    oneapi::tbb::task_group tasks;
    std::optional<Job> queued;
    std::optional<ValidationCompletion> ready;
    std::optional<ValidationCompletion> published;
    std::shared_ptr<const ValidationResult> publishedResult;
    std::uint64_t activeBaseRequestId = 0;
    std::vector<int> activeChangedObjectIds;
    DocumentIdentity requestedDocument;
    std::uint64_t requestedStructureRevision = 0;
    std::uint64_t requestedValueRevision = 0;
    std::uint64_t latestRequestId = 0;
    bool hasLatestRequest = false;
    bool active = false;
    bool stopping = false;
    std::function<void()> wakeCallback;

    void launch(Job job)
    {
        auto ownedJob = std::make_shared<Job>(std::move(job));
        std::function<void()> wake;
        {
            std::lock_guard lock(mutex);
            activeBaseRequestId = ownedJob->baseRequestId;
            activeChangedObjectIds = ownedJob->replacedObjectIds;
            wake = wakeCallback;
        }
        tasks.run([this, ownedJob, wake] {
            Job &job = *ownedJob;
            try
            {
                if (!job.incremental)
                {
                    work::record(work::Counter::ValidationFullDocuments);
                    job.completion.result = validate_document(job.snapshot, job.behaviorCatalog);
                }
                else
                {
                    job.completion.result = *job.baseResult;
                    std::unordered_set<int> replaced(job.replacedObjectIds.begin(),
                                                     job.replacedObjectIds.end());
                    std::erase_if(job.completion.result.assetDependencies,
                        [&](const ValidationAssetDependency &dependency) {
                            return replaced.contains(dependency.objectId);
                        });
                    std::erase_if(job.completion.result.issues, [&](const ValidationIssue &issue) {
                        return issue.objectId && replaced.contains(*issue.objectId);
                    });
                    for (const auto &local : job.localObjects)
                    {
                        validate_object_fields(job.snapshot, local.input,
                                               job.behaviorCatalog, job.completion.result);
                        work::record(work::Counter::ValidationLocalObjects);
                    }
                    for (const int objectId : job.inputFailures)
                        add_issue(job.completion.result, ValidationSeverity::Error, objectId,
                                  std::nullopt, "$", "Object exceeds the bounded local validation input limits.",
                                  true);
                    job.completion.result.blocksRun = !job.completion.result.structurallyValid;
                    for (const auto &issue : job.completion.result.issues)
                        job.completion.result.blocksRun |= issue.blocksRun;
                }
            }
            catch (const std::exception &error)
            {
                job.completion.result.structurallyValid = false;
                job.completion.result.blocksRun = true;
                job.completion.result.issues.push_back(
                    {ValidationSeverity::Error, std::nullopt, std::nullopt, "$",
                     std::string("Validation failed: ") + error.what(), true});
            }
            catch (...)
            {
                job.completion.result.structurallyValid = false;
                job.completion.result.blocksRun = true;
                job.completion.result.issues.push_back(
                    {ValidationSeverity::Error, std::nullopt, std::nullopt, "$",
                     "Validation failed with an unknown error.", true});
            }
            bind_result_identity(job.completion);

            std::optional<Job> next;
            {
                std::lock_guard lock(mutex);
                if (!stopping && job.completion.requestId == latestRequestId)
                {
                    published = job.completion;
                    publishedResult = std::make_shared<ValidationResult>(job.completion.result);
                    ready = std::move(job.completion);
                }
                if (!stopping && queued)
                {
                    next = std::move(queued);
                    queued.reset();
                }
                else
                {
                    active = false;
                    activeBaseRequestId = 0;
                    activeChangedObjectIds.clear();
                }
            }
            if (next)
                launch(std::move(*next));
            if (wake)
            {
                try { wake(); }
                catch (...) {}
            }
        });
    }
};

ValidationService::ValidationService() : impl_(std::make_unique<Impl>())
{
}

ValidationService::~ValidationService()
{
    if (!impl_)
        return;
    {
        std::lock_guard lock(impl_->mutex);
        impl_->stopping = true;
        impl_->queued.reset();
        impl_->ready.reset();
        impl_->published.reset();
    }
    impl_->tasks.wait();
}

void ValidationService::set_wake_callback(std::function<void()> callback)
{
    std::lock_guard lock(impl_->mutex);
    impl_->wakeCallback = std::move(callback);
}

std::uint64_t ValidationService::request(const DocumentSession &document, bool forceFresh,
                                         const Json &behaviorCatalog)
{
    std::optional<Impl::Job> job;
    std::uint64_t requestId = 0;
    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->stopping)
            return 0;
        const DocumentIdentity identity{ProjectGeneration{document.projectGeneration}, document.tabId,
                                        DocumentGeneration{document.documentGeneration}};
        if (impl_->hasLatestRequest && impl_->requestedDocument == identity &&
            impl_->requestedStructureRevision == document.structureRevision &&
            impl_->requestedValueRevision == document.valueRevision && !forceFresh)
            return impl_->latestRequestId;
        const bool overLimit = validation_snapshot_exceeds_limits(document);
        if (impl_->latestRequestId == std::numeric_limits<std::uint64_t>::max())
            impl_->latestRequestId = 0;
        requestId = ++impl_->latestRequestId;
        impl_->hasLatestRequest = true;
        impl_->requestedDocument = identity;
        impl_->requestedStructureRevision = document.structureRevision;
        impl_->requestedValueRevision = document.valueRevision;
        impl_->ready.reset();
        ValidationCompletion completion{identity,
                                         document.structureRevision, document.valueRevision, requestId, {}};
        if (overLimit)
        {
            completion.result.structurallyValid = false;
            completion.result.blocksRun = true;
            completion.result.issues.push_back(
                {ValidationSeverity::Error, std::nullopt, std::nullopt, "$",
                 "Scene exceeds the bounded validation snapshot limits.", true});
            bind_result_identity(completion);
            impl_->ready = completion;
            impl_->published = completion;
            impl_->publishedResult = std::make_shared<ValidationResult>(completion.result);
            impl_->queued.reset();
            return requestId;
        }
        Impl::Job fullJob;
        fullJob.completion = std::move(completion);
        fullJob.snapshot = document;
        fullJob.behaviorCatalog = behaviorCatalog;
        job.emplace(std::move(fullJob));
        if (impl_->active)
        {
            impl_->queued = std::move(job);
            job.reset();
        }
        else
        {
            impl_->active = true;
        }
    }

    if (job)
        impl_->launch(std::move(*job));
    return requestId;
}

std::uint64_t ValidationService::request_changed(const DocumentSession &document,
                                                const std::vector<int> &changedObjectIds,
                                                const Json &behaviorCatalog)
{
    if (changedObjectIds.empty() ||
        !document.indexes.current_for(document.documentGeneration, document.structureRevision) ||
        changedObjectIds.size() > 4096)
        return request(document, false, behaviorCatalog);

    const DocumentIdentity identity{ProjectGeneration{document.projectGeneration}, document.tabId,
                                    DocumentGeneration{document.documentGeneration}};
    std::optional<Impl::Job> job;
    std::uint64_t requestId = 0;
    bool useFullValidation = false;
    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->stopping)
            return 0;
        if (!impl_->published || !impl_->publishedResult ||
            impl_->published->document != identity ||
            impl_->published->structureRevision != document.structureRevision)
            useFullValidation = true;
        else if (impl_->queued && !impl_->queued->incremental &&
                 (impl_->queued->completion.document != identity ||
                  impl_->queued->completion.structureRevision != document.structureRevision ||
                  impl_->queued->completion.valueRevision != document.valueRevision))
            useFullValidation = true;
        else
        {
            const auto baseResult = impl_->publishedResult;
            const auto baseRequestId = impl_->published->requestId;
            std::vector<int> replacedIds;
            replacedIds.reserve(changedObjectIds.size());
            std::unordered_set<int> replacedSet;
            replacedSet.reserve(changedObjectIds.size());
            for (const int objectId : changedObjectIds)
                if (replacedSet.insert(objectId).second)
                    replacedIds.push_back(objectId);
            const auto append_unique = [&](int objectId) {
                if (replacedSet.insert(objectId).second)
                    replacedIds.push_back(objectId);
            };
            if (impl_->active && impl_->activeBaseRequestId == baseRequestId)
                for (const int objectId : impl_->activeChangedObjectIds)
                    append_unique(objectId);
            if (impl_->queued && impl_->queued->incremental &&
                impl_->queued->baseRequestId == baseRequestId)
                for (const int objectId : impl_->queued->replacedObjectIds)
                    append_unique(objectId);
            if (replacedIds.size() > 4096)
                useFullValidation = true;
            else
            {
                if (impl_->latestRequestId == std::numeric_limits<std::uint64_t>::max())
                    impl_->latestRequestId = 0;
                requestId = ++impl_->latestRequestId;
                impl_->hasLatestRequest = true;
                impl_->requestedDocument = identity;
                impl_->requestedStructureRevision = document.structureRevision;
                impl_->requestedValueRevision = document.valueRevision;
                impl_->ready.reset();

                Impl::Job changedJob;
                changedJob.incremental = true;
                changedJob.completion = ValidationCompletion{identity, document.structureRevision,
                    document.valueRevision, requestId, {}};
                changedJob.snapshot.assetRoot = document.assetRoot;
                changedJob.behaviorCatalog = behaviorCatalog;
                changedJob.baseResult = baseResult;
                changedJob.baseRequestId = baseRequestId;
                changedJob.replacedObjectIds = std::move(replacedIds);
                changedJob.completion.incremental = true;
                changedJob.completion.refreshedObjectIds = changedJob.replacedObjectIds;
                changedJob.localObjects.reserve(changedJob.replacedObjectIds.size());
                for (const int objectId : changedJob.replacedObjectIds)
                {
                    Json input;
                    if (!make_local_validation_input(document, objectId, input))
                        changedJob.inputFailures.push_back(objectId);
                    else
                        changedJob.localObjects.push_back({objectId, std::move(input)});
                }
                work::record(work::Counter::ValidationMergedObjects,
                             changedJob.replacedObjectIds.size());
                job.emplace(std::move(changedJob));
                if (impl_->active)
                {
                    impl_->queued = std::move(job);
                    job.reset();
                }
                else
                    impl_->active = true;
            }
        }
    }

    if (useFullValidation)
    {
        cancel();
        return request(document, false, behaviorCatalog);
    }
    if (job)
        impl_->launch(std::move(*job));
    return requestId;
}

std::optional<ValidationCompletion> ValidationService::take_current(const DocumentSession &document)
{
    const DocumentIdentity identity{ProjectGeneration{document.projectGeneration}, document.tabId,
                                    DocumentGeneration{document.documentGeneration}};
    std::lock_guard lock(impl_->mutex);
    if (!impl_->ready)
        return std::nullopt;
    const auto &completion = *impl_->ready;
    if (completion.requestId != impl_->latestRequestId || completion.document != identity ||
        completion.structureRevision != document.structureRevision ||
        completion.valueRevision != document.valueRevision)
    {
        impl_->ready.reset();
        return std::nullopt;
    }
    auto result = std::move(impl_->ready);
    impl_->ready.reset();
    return result;
}

void ValidationService::cancel() noexcept
{
    std::lock_guard lock(impl_->mutex);
    if (impl_->latestRequestId == std::numeric_limits<std::uint64_t>::max())
        impl_->latestRequestId = 0;
    else
        ++impl_->latestRequestId;
    impl_->queued.reset();
    impl_->ready.reset();
    impl_->published.reset();
    impl_->publishedResult.reset();
    impl_->activeBaseRequestId = 0;
    impl_->activeChangedObjectIds.clear();
    impl_->hasLatestRequest = false;
}

bool ValidationService::pending() const noexcept
{
    std::lock_guard lock(impl_->mutex);
    return impl_->active || impl_->queued.has_value();
}

bool ValidationService::is_current(const DocumentSession &document) const noexcept
{
    const DocumentIdentity identity{ProjectGeneration{document.projectGeneration}, document.tabId,
                                    DocumentGeneration{document.documentGeneration}};
    std::lock_guard lock(impl_->mutex);
    if (!impl_->published)
        return false;
    const auto &completion = *impl_->published;
    return completion.requestId == impl_->latestRequestId && completion.document == identity &&
           completion.structureRevision == document.structureRevision &&
           completion.valueRevision == document.valueRevision;
}

bool ValidationService::is_current(const DocumentSession &document,
                                   const ValidationResult &result) const noexcept
{
    const DocumentIdentity identity{ProjectGeneration{document.projectGeneration}, document.tabId,
                                    DocumentGeneration{document.documentGeneration}};
    std::lock_guard lock(impl_->mutex);
    if (!impl_->published || !impl_->publishedResult)
        return false;
    const auto &completion = *impl_->published;
    return completion.requestId == impl_->latestRequestId && completion.document == identity &&
           completion.structureRevision == document.structureRevision &&
           completion.valueRevision == document.valueRevision &&
           result.document == completion.document &&
           result.structureRevision == completion.structureRevision &&
           result.valueRevision == completion.valueRevision &&
           result.requestId == completion.requestId &&
           impl_->publishedResult->document == completion.document &&
           impl_->publishedResult->structureRevision == completion.structureRevision &&
           impl_->publishedResult->valueRevision == completion.valueRevision &&
           impl_->publishedResult->requestId == completion.requestId;
}

bool validation_allows_run(const ValidationService &service,
                           const DocumentSession &document,
                           const ValidationResult &result) noexcept
{
    return service.is_current(document, result) && result.valid_for_run();
}
} // namespace platformator_editor
