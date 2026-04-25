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