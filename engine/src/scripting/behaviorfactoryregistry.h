#pragma once

#include <functional>
#include <optional>
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
    void registerInspectionProvider(const std::string &typeName, platformator::ScriptInspectionProvider provider);
    platformator::ScriptInspectionResult inspectFields(std::string_view typeName, const Behavior &instance,
                                                       platformator::ScriptInspectionLimits limits) const;
    Behavior *createBehavior(std::string_view typeName) const;
    std::optional<platformator::ScriptDescriptor> findDescriptor(std::string_view typeName) const;
    std::vector<platformator::ScriptDescriptor> registeredDescriptors() const;

private:
    struct TransparentStringHash
    {
        using is_transparent = void;
        std::size_t operator()(std::string_view value) const noexcept
        {
            return std::hash<std::string_view>{}(value);
        }
        std::size_t operator()(const std::string &value) const noexcept
        {
            return std::hash<std::string_view>{}(value);
        }
    };

    BehaviorFactoryRegistry() = default;
    ~BehaviorFactoryRegistry() = default;
    std::unordered_map<std::string, std::function<Behavior *()>, TransparentStringHash, std::equal_to<>> factories;
    std::unordered_map<std::string, std::vector<platformator::ScriptDescriptorProvider>, TransparentStringHash,
                       std::equal_to<>>
        descriptorProviders;
    std::unordered_map<std::string, platformator::ScriptInspectionProvider, TransparentStringHash, std::equal_to<>>
        inspectionProviders;
};
