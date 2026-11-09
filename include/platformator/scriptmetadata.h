#pragma once

#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <list>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <Eigen/Dense>
#include <SDL3/SDL.h>
#include <json.hpp>

#include "platformator/rigidbody.h"

template <typename T> class AssetReference;
template <typename T> class ObjectReference;
class AnimationClip;
class Animator;
class Audio;
class AudioWrapper;
class Behavior;
class Camera;
class Collider;
class ScriptComponent;
class Sprite;
class TextureWrapper;

namespace platformator
{
enum class ScriptFieldKind
{
    Boolean,
    SignedInteger,
    UnsignedInteger,
    Number,
    String,
    Enum,
    Vector2,
    Rectangle,
    Array,
    Optional,
    AssetReference,
    ObjectReference,
    OpaqueJson
};

struct ScriptEnumValue
{
    std::string name;
    std::int64_t value = 0;
};

struct ScriptFieldDescriptor
{
    std::string name;
    ScriptFieldKind kind = ScriptFieldKind::OpaqueJson;
    std::optional<std::string> minimum;
    std::optional<std::string> maximum;
    std::string targetType;
    bool nullable = false;
    std::vector<ScriptEnumValue> enumValues;
    std::shared_ptr<ScriptFieldDescriptor> element;
};

struct ScriptDescriptor
{
    std::string name;
    bool fieldSchemaAvailable = false;
    std::vector<ScriptFieldDescriptor> fields;
};

using ScriptDescriptorProvider = ScriptDescriptor (*)();

struct ScriptInspectionLimits
{
    std::size_t maxFields = 64;
    std::size_t maxStringBytes = 256;
    std::size_t maxArrayItems = 16;
    std::size_t maxOutputBytes = 16 * 1024;
};

struct ScriptFieldSnapshot
{
    std::string name;
    std::string kind;
    bool available = false;
    bool truncated = false;
    nlohmann::json value;
    std::string unavailableReason;
};

struct ScriptInspectionResult
{
    bool providerAvailable = false;
    bool outputTruncated = false;
    std::size_t omittedFieldCount = 0;
    std::size_t retainedOutputBytes = 0;
    std::vector<ScriptFieldSnapshot> fields;
};

// Accessors read declared members directly; they do not call
// Behavior::serialize() or retain the instance.
using ScriptInspectionProvider = ScriptInspectionResult (*)(const Behavior &, const ScriptInspectionLimits &);

ScriptInspectionLimits bounded_script_inspection_limits(ScriptInspectionLimits limits) noexcept;

const char *script_field_kind_name(ScriptFieldKind kind) noexcept;
nlohmann::json script_field_descriptor_json(const ScriptFieldDescriptor &field);
nlohmann::json script_descriptor_json(const ScriptDescriptor &descriptor);
nlohmann::json export_registered_behavior_catalog();

namespace metadata_detail
{
template <typename T, typename Enable = void> struct ScriptFieldTraits
{
    static ScriptFieldDescriptor describe(std::string_view name)
    {
        return {std::string(name), ScriptFieldKind::OpaqueJson};
    }
};

template <> struct ScriptFieldTraits<bool>
{
    static ScriptFieldDescriptor describe(std::string_view name)
    {
        return {std::string(name), ScriptFieldKind::Boolean};
    }
};

template <typename T> struct ScriptFieldTraits<T, std::enable_if_t<std::is_integral_v<T> && std::is_signed_v<T>>>
{
    static ScriptFieldDescriptor describe(std::string_view name)
    {
        ScriptFieldDescriptor descriptor{std::string(name), ScriptFieldKind::SignedInteger};
        descriptor.minimum = std::to_string(static_cast<std::int64_t>(std::numeric_limits<T>::lowest()));
        descriptor.maximum = std::to_string(static_cast<std::int64_t>(std::numeric_limits<T>::max()));
        return descriptor;
    }
};

template <typename T>
struct ScriptFieldTraits<T, std::enable_if_t<std::is_integral_v<T> && std::is_unsigned_v<T> &&
                                              !std::is_same_v<T, bool>>>
{
    static ScriptFieldDescriptor describe(std::string_view name)
    {
        ScriptFieldDescriptor descriptor{std::string(name), ScriptFieldKind::UnsignedInteger};
        descriptor.minimum = "0";
        descriptor.maximum = std::to_string(static_cast<std::uint64_t>(std::numeric_limits<T>::max()));
        return descriptor;
    }
};

template <typename T> struct ScriptFieldTraits<T, std::enable_if_t<std::is_floating_point_v<T>>>
{
    static ScriptFieldDescriptor describe(std::string_view name)
    {
        return {std::string(name), ScriptFieldKind::Number};
    }
};

template <> struct ScriptFieldTraits<std::string>
{
    static ScriptFieldDescriptor describe(std::string_view name)
    {
        return {std::string(name), ScriptFieldKind::String};
    }
};

template <typename T> std::vector<ScriptEnumValue> known_enum_values()
{
    return {};
}

template <> inline std::vector<ScriptEnumValue> known_enum_values<BodyType>()
{
    return {{"DYNAMIC", DYNAMIC}, {"STATIC", STATIC}, {"KINEMATIC", KINEMATIC}};
}

template <> inline std::vector<ScriptEnumValue> known_enum_values<SDL_FlipMode>()
{
    return {{"NONE", SDL_FLIP_NONE}, {"HORIZONTAL", SDL_FLIP_HORIZONTAL},
            {"VERTICAL", SDL_FLIP_VERTICAL}, {"HORIZONTAL_AND_VERTICAL", SDL_FLIP_HORIZONTAL_AND_VERTICAL}};
}

template <typename T> struct ScriptFieldTraits<T, std::enable_if_t<std::is_enum_v<T>>>
{
    static ScriptFieldDescriptor describe(std::string_view name)
    {
        ScriptFieldDescriptor descriptor{std::string(name), ScriptFieldKind::Enum};
        descriptor.enumValues = known_enum_values<T>();
        return descriptor;
    }
};

template <> struct ScriptFieldTraits<Eigen::Vector2f>
{
    static ScriptFieldDescriptor describe(std::string_view name)
    {
        return {std::string(name), ScriptFieldKind::Vector2};
    }
};

template <> struct ScriptFieldTraits<SDL_FRect>
{
    static ScriptFieldDescriptor describe(std::string_view name)
    {
        return {std::string(name), ScriptFieldKind::Rectangle};
    }
};

template <typename T, typename Allocator> struct ScriptFieldTraits<std::vector<T, Allocator>>
{
    static ScriptFieldDescriptor describe(std::string_view name)
    {
        ScriptFieldDescriptor descriptor{std::string(name), ScriptFieldKind::Array};
        descriptor.element = std::make_shared<ScriptFieldDescriptor>(ScriptFieldTraits<T>::describe("element"));
        return descriptor;
    }
};

template <typename T, typename Allocator> struct ScriptFieldTraits<std::deque<T, Allocator>>
{
    static ScriptFieldDescriptor describe(std::string_view name)
    {
        ScriptFieldDescriptor descriptor{std::string(name), ScriptFieldKind::Array};
        descriptor.element = std::make_shared<ScriptFieldDescriptor>(ScriptFieldTraits<T>::describe("element"));
        return descriptor;
    }
};

template <typename T, typename Allocator> struct ScriptFieldTraits<std::list<T, Allocator>>
{
    static ScriptFieldDescriptor describe(std::string_view name)
    {
        ScriptFieldDescriptor descriptor{std::string(name), ScriptFieldKind::Array};
        descriptor.element = std::make_shared<ScriptFieldDescriptor>(ScriptFieldTraits<T>::describe("element"));
        return descriptor;
    }
};

template <typename T, std::size_t Size> struct ScriptFieldTraits<std::array<T, Size>>
{
    static ScriptFieldDescriptor describe(std::string_view name)
    {
        ScriptFieldDescriptor descriptor{std::string(name), ScriptFieldKind::Array};
        descriptor.element = std::make_shared<ScriptFieldDescriptor>(ScriptFieldTraits<T>::describe("element"));
        return descriptor;
    }
};

template <typename T> struct ScriptFieldTraits<std::optional<T>>
{
    static ScriptFieldDescriptor describe(std::string_view name)
    {
        ScriptFieldDescriptor descriptor{std::string(name), ScriptFieldKind::Optional};
        descriptor.nullable = true;
        descriptor.element = std::make_shared<ScriptFieldDescriptor>(ScriptFieldTraits<T>::describe("value"));
        return descriptor;
    }
};

template <typename T> struct ObjectTargetName
{
    static constexpr std::string_view value = "unknown";
};

template <> struct ObjectTargetName<GameObject>
{
    static constexpr std::string_view value = "GameObject";
};
template <> struct ObjectTargetName<Component>
{
    static constexpr std::string_view value = "Component";
};
template <> struct ObjectTargetName<Camera>
{
    static constexpr std::string_view value = "Camera";
};
template <> struct ObjectTargetName<Collider>
{
    static constexpr std::string_view value = "Collider";
};
template <> struct ObjectTargetName<Rigidbody>
{
    static constexpr std::string_view value = "Rigidbody";
};
template <> struct ObjectTargetName<Sprite>
{
    static constexpr std::string_view value = "Sprite";
};
template <> struct ObjectTargetName<Animator>
{
    static constexpr std::string_view value = "Animator";
};
template <> struct ObjectTargetName<Audio>
{
    static constexpr std::string_view value = "Audio";
};
template <> struct ObjectTargetName<ScriptComponent>
{
    static constexpr std::string_view value = "ScriptComponent";
};

template <typename T> struct AssetTargetName
{
    static constexpr std::string_view value = "Asset";
};
template <> struct AssetTargetName<TextureWrapper>
{
    static constexpr std::string_view value = "Texture";
};
template <> struct AssetTargetName<AudioWrapper>
{
    static constexpr std::string_view value = "Audio";
};
template <> struct AssetTargetName<AnimationClip>
{
    static constexpr std::string_view value = "AnimationClip";
};

template <typename T> struct ScriptFieldTraits<ObjectReference<T>>
{
    static ScriptFieldDescriptor describe(std::string_view name)
    {
        ScriptFieldDescriptor descriptor{std::string(name), ScriptFieldKind::ObjectReference};
        descriptor.targetType = ObjectTargetName<T>::value;
        descriptor.nullable = true;
        return descriptor;
    }
};

template <typename T> struct ScriptFieldTraits<AssetReference<T>>
{
    static ScriptFieldDescriptor describe(std::string_view name)
    {
        ScriptFieldDescriptor descriptor{std::string(name), ScriptFieldKind::AssetReference};
        descriptor.targetType = AssetTargetName<T>::value;
        descriptor.nullable = true;
        return descriptor;
    }
};

template <typename T> ScriptFieldDescriptor describe_script_field(std::string_view name, T *)
{
    return ScriptFieldTraits<std::remove_cv_t<T>>::describe(name);
}

template <typename Owner, typename T> ScriptFieldDescriptor describe_script_field(std::string_view name,
                                                                                    T Owner::*)
{
    return ScriptFieldTraits<std::remove_cv_t<T>>::describe(name);
}
} // namespace metadata_detail

template <typename Owner, typename T> ScriptFieldDescriptor describe_script_field(std::string_view name,
                                                                                  T Owner::*member)
{
    return metadata_detail::describe_script_field(name, member);
}

namespace metadata_detail
{
template <typename T> struct IsOptional : std::false_type
{
};
template <typename T> struct IsOptional<std::optional<T>> : std::true_type
{
};

template <typename T> struct IsObjectReference : std::false_type
{
};
template <typename T> struct IsObjectReference<::ObjectReference<T>> : std::true_type
{
    using Target = T;
};

template <typename T> struct IsAssetReference : std::false_type
{
};
template <typename T> struct IsAssetReference<::AssetReference<T>> : std::true_type
{
    using Target = T;
};

template <typename T> struct IsBoundedSequence : std::false_type
{
};
template <typename T, typename Allocator>
struct IsBoundedSequence<std::vector<T, Allocator>> : std::true_type
{
};
template <typename T, typename Allocator>
struct IsBoundedSequence<std::deque<T, Allocator>> : std::true_type
{
};
template <typename T, typename Allocator>
struct IsBoundedSequence<std::list<T, Allocator>> : std::true_type
{
};
template <typename T, std::size_t Size>
struct IsBoundedSequence<std::array<T, Size>> : std::true_type
{
};

inline std::string bounded_utf8_prefix(std::string_view text, std::size_t byteLimit, bool &truncated)
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

        const bool validLeading = first <= 0x7f || (first >= 0xc2 && first <= 0xf4);
        bool valid = validLeading && offset + width <= text.size();
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
    if (offset < text.size())
        truncated = true;
    return result;
}

inline ScriptInspectionLimits normalize_inspection_limits(ScriptInspectionLimits limits) noexcept
{
    limits.maxFields = std::min<std::size_t>(limits.maxFields, 64);
    limits.maxStringBytes = std::min<std::size_t>(limits.maxStringBytes, 1024);
    limits.maxArrayItems = std::min<std::size_t>(limits.maxArrayItems, 32);
    limits.maxOutputBytes = std::clamp<std::size_t>(limits.maxOutputBytes, 256, 64 * 1024);
    return limits;
}

template <typename T>
nlohmann::json inspect_script_value(const T &value, const ScriptInspectionLimits &limits,
                                    bool &available, bool &truncated,
                                    std::size_t &remainingArrayItems,
                                    std::size_t &remainingStringBytes)
{
    using Value = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<Value, bool>)
        return value;
    else if constexpr (std::is_integral_v<Value> && std::is_signed_v<Value>)
        return static_cast<std::int64_t>(value);
    else if constexpr (std::is_integral_v<Value> && std::is_unsigned_v<Value>)
        return static_cast<std::uint64_t>(value);
    else if constexpr (std::is_floating_point_v<Value>)
    {
        if (std::isfinite(value))
            return static_cast<double>(value);
        if (std::isnan(value))
            return "NaN";
        return value < 0 ? nlohmann::json("-Infinity") : nlohmann::json("Infinity");
    }
    else if constexpr (std::is_same_v<Value, std::string>)
    {
        auto result = bounded_utf8_prefix(value, std::min(limits.maxStringBytes, remainingStringBytes), truncated);
        remainingStringBytes -= result.size();
        return result;
    }
    else if constexpr (std::is_enum_v<Value>)
    {
        using Underlying = std::underlying_type_t<Value>;
        const auto numeric = static_cast<Underlying>(value);
        nlohmann::json result{{"value", numeric}};
        for (const auto &known : known_enum_values<Value>())
            if (static_cast<std::int64_t>(known.value) == static_cast<std::int64_t>(numeric))
            {
                result["name"] = known.name;
                break;
            }
        return result;
    }
    else if constexpr (std::is_same_v<Value, Eigen::Vector2f>)
        return nlohmann::json{{"x", static_cast<double>(value.x())},
                              {"y", static_cast<double>(value.y())}};
    else if constexpr (std::is_same_v<Value, SDL_FRect>)
        return nlohmann::json{{"x", static_cast<double>(value.x)},
                              {"y", static_cast<double>(value.y)},
                              {"w", static_cast<double>(value.w)},
                              {"h", static_cast<double>(value.h)}};
    else if constexpr (IsOptional<Value>::value)
    {
        if (!value)
            return nullptr;
        return inspect_script_value(*value, limits, available, truncated,
                                    remainingArrayItems, remainingStringBytes);
    }
    else if constexpr (IsObjectReference<Value>::value)
    {
        const auto handle = value.getHandle();
        const auto fileId = value.getReferencedId();
        nlohmann::json summary{
            {"target_kind", std::string(ObjectTargetName<typename IsObjectReference<Value>::Target>::value)},
            {"state", handle ? (fileId ? "resolved" : "stale") : (fileId ? "unresolved" : "empty")},
            {"file_id", fileId ? nlohmann::json(*fileId) : nlohmann::json(nullptr)}};
        if (handle)
            summary["handle"] = {{"runtime_serial", handle.runtimeSerial},
                                 {"slot", handle.slot}, {"generation", handle.generation},
                                 {"kind", std::string(ObjectTargetName<typename IsObjectReference<Value>::Target>::value)}};
        else
            summary["handle"] = nullptr;
        return summary;
    }
    else if constexpr (IsAssetReference<Value>::value)
    {
        if (!value.hasFilePath())
            return nlohmann::json{{"target_kind", "asset"}, {"assigned", false}};
        auto boundedPath = bounded_utf8_prefix(value.getFilePathView(),
            std::min(limits.maxStringBytes, remainingStringBytes), truncated);
        remainingStringBytes -= boundedPath.size();
        return nlohmann::json{{"target_kind", std::string(AssetTargetName<typename IsAssetReference<Value>::Target>::value)},
                              {"assigned", true}, {"runtime_serial", value.getRuntimeSerial()},
                              {"path", boundedPath}};
    }
    else if constexpr (IsBoundedSequence<Value>::value)
    {
        nlohmann::json result = nlohmann::json::array();
        for (const auto &element : value)
        {
            if (remainingArrayItems == 0)
            {
                truncated = true;
                break;
            }
            --remainingArrayItems;
            bool elementAvailable = true;
            bool elementTruncated = false;
            auto item = inspect_script_value(element, limits, elementAvailable, elementTruncated,
                                             remainingArrayItems, remainingStringBytes);
            if (!elementAvailable)
            {
                available = false;
                return nullptr;
            }
            result.push_back(std::move(item));
            truncated = truncated || elementTruncated;
        }
        return result;
    }
    else
    {
        available = false;
        return nullptr;
    }
}

template <typename T>
void append_script_field(ScriptInspectionResult &result, std::string_view name, const T &value,
                         const ScriptInspectionLimits &requestedLimits)
{
    const auto limits = normalize_inspection_limits(requestedLimits);
    if (result.fields.size() >= limits.maxFields || result.outputTruncated)
    {
        ++result.omittedFieldCount;
        result.outputTruncated = true;
        return;
    }

    using Value = std::remove_cvref_t<T>;
    const auto descriptor = ScriptFieldTraits<Value>::describe(name);
    bool available = true;
    bool truncated = false;
    std::size_t remainingArrayItems = limits.maxArrayItems;
    std::size_t remainingStringBytes = limits.maxStringBytes;
    auto jsonValue = inspect_script_value(value, limits, available, truncated,
                                          remainingArrayItems, remainingStringBytes);
    const auto reason = available ? std::string{} :
        std::string("field type is not allowlisted for read-only inspection");
    const std::size_t projectedBytes = name.size() + std::string(script_field_kind_name(descriptor.kind)).size() +
        reason.size() + jsonValue.dump().size() + 64;
    if (projectedBytes > limits.maxOutputBytes - result.retainedOutputBytes)
    {
        ++result.omittedFieldCount;
        result.outputTruncated = true;
        return;
    }
    result.fields.push_back({std::string(name), script_field_kind_name(descriptor.kind), available,
                             truncated, std::move(jsonValue), reason});
    result.retainedOutputBytes += projectedBytes;
}

} // namespace metadata_detail

inline ScriptInspectionLimits bounded_script_inspection_limits(ScriptInspectionLimits limits) noexcept
{
    return metadata_detail::normalize_inspection_limits(limits);
}
} // namespace platformator
