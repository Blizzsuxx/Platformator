#pragma once

#include <cstdint>
#include <compare>
#include <limits>
#include <type_traits>

class BaseObject;
class Component;
class GameObject;
class Animator;
class Audio;
class Camera;
class Collider;
class BoxCollider;
class CircleCollider;
class Rigidbody;
class ScriptComponent;
class Sprite;

namespace platformator
{
struct ObjectHandle
{
    uint32_t slot = std::numeric_limits<uint32_t>::max();
    uint64_t generation = 0;
    uint64_t runtimeSerial = 0;
    explicit operator bool() const
    {
        return slot != std::numeric_limits<uint32_t>::max();
    }
    bool operator==(const ObjectHandle &) const = default;
    auto operator<=>(const ObjectHandle &) const = default;
};
} // namespace platformator

namespace platformator_detail
{
enum class ObjectKind : uint8_t
{
    BaseObject,
    Component,
    Collider,
    GameObject,
    Animator,
    Audio,
    Camera,
    BoxCollider,
    CircleCollider,
    Rigidbody,
    ScriptComponent,
    Sprite
};
template <typename T> constexpr ObjectKind objectKind()
{
    if constexpr (std::is_same_v<T, BaseObject>)
        return ObjectKind::BaseObject;
    else if constexpr (std::is_same_v<T, Component>)
        return ObjectKind::Component;
    else if constexpr (std::is_same_v<T, Collider>)
        return ObjectKind::Collider;
    else if constexpr (std::is_same_v<T, GameObject>)
        return ObjectKind::GameObject;
    else if constexpr (std::is_same_v<T, Animator>)
        return ObjectKind::Animator;
    else if constexpr (std::is_same_v<T, Audio>)
        return ObjectKind::Audio;
    else if constexpr (std::is_same_v<T, Camera>)
        return ObjectKind::Camera;
    else if constexpr (std::is_same_v<T, BoxCollider>)
        return ObjectKind::BoxCollider;
    else if constexpr (std::is_same_v<T, CircleCollider>)
        return ObjectKind::CircleCollider;
    else if constexpr (std::is_same_v<T, Rigidbody>)
        return ObjectKind::Rigidbody;
    else if constexpr (std::is_same_v<T, ScriptComponent>)
        return ObjectKind::ScriptComponent;
    else if constexpr (std::is_same_v<T, Sprite>)
        return ObjectKind::Sprite;
    else
        static_assert(std::is_same_v<T, void>, "Unsupported persistent reference kind");
}
} // namespace platformator_detail
