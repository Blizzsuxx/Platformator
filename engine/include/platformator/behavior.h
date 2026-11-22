#pragma once

#include <cstdint>
#include <string>
#include <type_traits>

#include <json.hpp>

#include "platformator/gameobject.h"

class Collision;
class Collider;

enum BehaviorUpdateMask : uint8_t
{
    BEHAVIOR_UPDATE_NONE = 0,
    BEHAVIOR_UPDATE_FIXED = 1 << 0,
    BEHAVIOR_UPDATE_FRAME = 1 << 1,
    BEHAVIOR_UPDATE_LATE = 1 << 2,
    BEHAVIOR_UPDATE_ALL = BEHAVIOR_UPDATE_FIXED | BEHAVIOR_UPDATE_FRAME | BEHAVIOR_UPDATE_LATE,
};

namespace platformator
{
class Runtime;
class ReferenceResolver;
} // namespace platformator

class Behavior
{
    friend class ScriptComponent;
    friend class Template;

  public:
    Behavior();
    Behavior(const Behavior &);
    virtual ~Behavior() = default;
    virtual Behavior *clone() const;

    GameObject *getGameObject() const;
    platformator::Runtime &getRuntime() const;

    template <typename T> T *getBehavior(GameObject *gameObject) const;

    void setGameObject(GameObject *gameObject);

    virtual void start();
    virtual void update(double timeDelta);
    virtual void fixedUpdate(double timeDelta);
    virtual void lateUpdate(double timeDelta);
    virtual void onDestroy();
    virtual void onCollisionEnter(const Collision *collision, Collider *other, double timeDelta);
    virtual void onCollisionExit(Collider *other, double timeDelta);
    virtual uint8_t getUpdateMask() const noexcept
    {
        return detectedUpdateMask;
    }

    virtual std::string getTypeName() const = 0;
    virtual void serialize(nlohmann::json &j) const = 0;
    virtual void deserialize(const nlohmann::json &j) = 0;
    virtual void resolveTemplateAssets(platformator::ReferenceResolver &) {}
    virtual void resolveReferences(platformator::ReferenceResolver &resolver) = 0;

  private:
    ScriptComponent *scriptComponent = nullptr;
    GameObject *gameObject = nullptr;
    uint8_t detectedUpdateMask = BEHAVIOR_UPDATE_ALL;
};

namespace platformator_detail
{
template <typename T>
constexpr uint8_t detectBehaviorUpdateMask() noexcept
{
    uint8_t mask = BEHAVIOR_UPDATE_NONE;
    if constexpr (requires { &T::fixedUpdate; })
    {
        if constexpr (!std::is_same_v<decltype(&T::fixedUpdate), decltype(&Behavior::fixedUpdate)>)
            mask |= BEHAVIOR_UPDATE_FIXED;
    }
    else
    {
        mask |= BEHAVIOR_UPDATE_FIXED;
    }
    if constexpr (requires { &T::update; })
    {
        if constexpr (!std::is_same_v<decltype(&T::update), decltype(&Behavior::update)>)
            mask |= BEHAVIOR_UPDATE_FRAME;
    }
    else
    {
        mask |= BEHAVIOR_UPDATE_FRAME;
    }
    if constexpr (requires { &T::lateUpdate; })
    {
        if constexpr (!std::is_same_v<decltype(&T::lateUpdate), decltype(&Behavior::lateUpdate)>)
            mask |= BEHAVIOR_UPDATE_LATE;
    }
    else
    {
        mask |= BEHAVIOR_UPDATE_LATE;
    }
    return mask;
}
} // namespace platformator_detail
