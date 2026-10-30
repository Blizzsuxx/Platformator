#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "platformator/scriptmetadata.h"

class Behavior;

class BehaviorFactoryRegistry
{
public:
    static BehaviorFactoryRegistry &getInstance()
    {
        static BehaviorFactoryRegistry instance;
        return instance;
    }

    BehaviorFactoryRegistry(const BehaviorFactoryRegistry &) = delete;
    BehaviorFactoryRegistry &operator=(const BehaviorFactoryRegistry &) = delete;

    void registerFactory(const std::string &typeName, std::function<Behavior *()> factory);
    void registerDescriptorProvider(const std::string &typeName, platformator::ScriptDescriptorProvider provider);
    Behavior *createBehavior(const std::string &typeName) const;
    std::vector<platformator::ScriptDescriptor> registeredDescriptors() const;

private:
    BehaviorFactoryRegistry() = default;
    ~BehaviorFactoryRegistry() = default;
    std::unordered_map<std::string, std::function<Behavior *()>> factories;
    std::unordered_map<std::string, std::vector<platformator::ScriptDescriptorProvider>> descriptorProviders;
};
