#include "validation.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>
#include <json.hpp>
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
                    bool positive = false, bool nonnegative = false)
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
                                        std::string &error)
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
                              int componentId, ValidationResult &result)
{
    const auto *value = member(component, field);
    if (value == nullptr || !value->is_string() || value->get_ref<const std::string &>().empty())
    {
        add_issue(result, ValidationSeverity::Error, objectId, componentId, std::move(fieldPath),
                  "A required asset path is missing.", true);
        return;
    }
    std::string resolutionError;
    const auto asset = resolve_asset_path(document, value->get_ref<const std::string &>(), resolutionError);
    if (asset.empty())
    {
        add_issue(result, ValidationSeverity::Error, objectId, componentId, std::move(fieldPath),
                  std::move(resolutionError), true);
        return;
    }
    std::error_code filesystemError;
    if (!std::filesystem::is_regular_file(asset, filesystemError) || filesystemError)
        add_issue(result, ValidationSeverity::Error, objectId, componentId, std::move(fieldPath),
                  "The referenced asset is missing or is not a file.", true);
}

void validate_camera(const Json &component, int objectId, int componentId, ValidationResult &result)
{
    const auto *legacy = member(component, "camera");
    const auto *legacyWidth = legacy ? member(*legacy, "w") : nullptr;
    const auto *legacyHeight = legacy ? member(*legacy, "h") : nullptr;
    const auto *width = member(component, "width");
    const auto *height = member(component, "height");

    auto validate_dimension = [&](const Json *canonical, const Json *oldValue, const char *name,
                                  const char *oldName) {
        if (canonical != nullptr && oldValue != nullptr)
        {
            const auto current = numeric_value(canonical);
            const auto old = numeric_value(oldValue);
            if (!current || !old || *current != *old)
            {
                add_issue(result, ValidationSeverity::Error, objectId, componentId, name,
                          std::string("Conflicting canonical and legacy camera ") + oldName +
                              " values require an explicit edit.", true);
                return;
            }
        }
        const Json *chosen = canonical != nullptr ? canonical : oldValue;
        if (chosen == nullptr)
        {
            add_issue(result, ValidationSeverity::Error, objectId, componentId, name,
                      "A camera dimension is required.", true);
            return;
        }
        const auto value = numeric_value(chosen);
        if (!value || !std::isfinite(*value) || *value <= 0.0)
            add_issue(result, ValidationSeverity::Error, objectId, componentId, name,
                      "Camera dimensions must be finite and positive.", true);
    };
    validate_dimension(width, legacyWidth, "width", "width");
    validate_dimension(height, legacyHeight, "height", "height");
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
        require_number(component, "width", "width", objectId, componentId, result, true);
        require_number(component, "height", "height", objectId, componentId, result, true);
    }
    else
        require_number(component, "radius", "radius", objectId, componentId, result, true);
    require_vector(component, "offset", "offset", objectId, componentId, result);
    require_boolean(component, "trigger", "trigger", objectId, componentId, result);

    for (const char *field : {"collisionGroup", "collisionMask"})
    {
        const auto *value = member(component, field);
        bool valid = value != nullptr && (value->is_number_integer() || value->is_number_unsigned());
        if (valid && value->is_number_integer())
            valid = integer_value(value).value_or(-1) >= 0;
        if (valid && value->is_number_unsigned())
            valid = true;
        if (!valid)
            add_issue(result, ValidationSeverity::Error, objectId, componentId, field,
                      "Collision groups and masks must be nonnegative integer bit masks.", true);
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
                             objectId, componentId, result);
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
                             objectId, componentId, result);
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
    validate_asset_reference(document, component, "filePath", "filePath", objectId, componentId, result);
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

void validate_component(const DocumentSession &document, const Json &component,
                        int objectId, ValidationResult &result)
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
        // Behavior-specific fields belong to the game. Catalog validation is applied when a target is selected.
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

} // namespace

ValidationResult validate_document(const DocumentSession &document)
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

        for (const auto &component : object.at("components"))
            validate_component(document, component, objectId, result);
        for (auto iter = object.at("children").rbegin(); iter != object.at("children").rend(); ++iter)
            pending.push_back({&*iter});
    }
    return result;
}
} // namespace platformator_editor
