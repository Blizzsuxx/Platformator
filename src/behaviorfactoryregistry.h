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

        registerFactory(type, []() -> Behavior *
                        { return new T(); });
    }

    void registerFactory(const std::string &type, Factory factory);
    Behavior *createBehavior(ScriptComponent *scriptComponent, const ScriptDescriptor &descriptor) const;

private:
    BehaviorFactoryRegistry() = default;

    std::unordered_map<std::string, Factory> factories;
};

#define PLATFORMATOR_BEHAVIOR_REGISTRY_CONCAT_IMPL(left, right) left##right
#define PLATFORMATOR_BEHAVIOR_REGISTRY_CONCAT(left, right) PLATFORMATOR_BEHAVIOR_REGISTRY_CONCAT_IMPL(left, right)

// The translation unit using this macro must be linked into the final binary.
#define REGISTER_BEHAVIOR_NAMED(TYPE, TYPE_NAME)                                                                   \
    namespace                                                                                                      \
    {                                                                                                              \
        [[maybe_unused]] const bool PLATFORMATOR_BEHAVIOR_REGISTRY_CONCAT(registeredBehavior_, __COUNTER__) = []() \
        {                                                                                                          \
            BehaviorFactoryRegistry::getInstance().registerBehavior<TYPE>(TYPE_NAME);                              \
            return true;                                                                                           \
        }();                                                                                                       \
    }

#define REGISTER_BEHAVIOR(TYPE) REGISTER_BEHAVIOR_NAMED(TYPE, #TYPE)