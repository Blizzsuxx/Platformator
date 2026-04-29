#pragma once

#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>

#include "behavior.h"

class ScriptComponent;

class BehaviorFactoryRegistry
{
public:
    using Factory = std::function<Behavior *()>;

    static BehaviorFactoryRegistry &getInstance()
    {
        static BehaviorFactoryRegistry instance;
        return instance;
    }

    template <typename T>
    void registerBehavior(const std::string &type)
    {
        static_assert(std::is_base_of_v<Behavior, T>, "registerBehavior<T> requires T to derive from Behavior");
        static_assert(std::is_default_constructible_v<T>, "registerBehavior<T> requires T to be default constructible");

        registerFactory(type, [type]() -> Behavior *
                        {
                            Behavior *behavior = new T();
                            behavior->setRegisteredTypeName(type);
                            return behavior; });
    }

    void registerFactory(const std::string &type, Factory factory);
    Behavior *instantiateBehavior(ScriptComponent *scriptComponent, const BehaviorSpec &spec) const;

private:
    BehaviorFactoryRegistry() = default;

    std::unordered_map<std::string, Factory> factories;
};

namespace platformator_behavior_registry_detail
{
    template <typename T>
    inline bool registerBehaviorType(const std::string &type)
    {
        BehaviorFactoryRegistry::getInstance().registerBehavior<T>(type);
        return true;
    }
}

#define PLATFORMATOR_BEHAVIOR_REGISTRY_CONCAT_IMPL(left, right) left##right
#define PLATFORMATOR_BEHAVIOR_REGISTRY_CONCAT(left, right) PLATFORMATOR_BEHAVIOR_REGISTRY_CONCAT_IMPL(left, right)

// Header-safe self-registration macro. Place it after the behavior class definition,
// typically in the same header and namespace as the class.
#define REGISTER_BEHAVIOR_NAMED(TYPE, TYPE_NAME)                                                                      \
    [[maybe_unused]] inline const bool PLATFORMATOR_BEHAVIOR_REGISTRY_CONCAT(platformatorRegisteredBehavior_, TYPE) = \
        ::platformator_behavior_registry_detail::registerBehaviorType<TYPE>(TYPE_NAME)

#define REGISTER_BEHAVIOR(TYPE) REGISTER_BEHAVIOR_NAMED(TYPE, #TYPE)