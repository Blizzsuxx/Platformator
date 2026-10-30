#pragma once

#include <array>
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
} // namespace platformator
