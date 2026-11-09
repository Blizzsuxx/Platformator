#include "behaviorfactoryregistry.h"
#include "platformator/behavior.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

void BehaviorFactoryRegistry::registerFactory(const std::string &typeName, std::function<Behavior *()> factory)
{
    factories[typeName] = std::move(factory);
}

void BehaviorFactoryRegistry::registerDescriptorProvider(
    const std::string &typeName, platformator::ScriptDescriptorProvider provider)
{
    if (provider != nullptr)
        descriptorProviders[typeName].push_back(provider);
}

void BehaviorFactoryRegistry::registerInspectionProvider(
    const std::string &typeName, platformator::ScriptInspectionProvider provider)
{
    if (provider != nullptr)
        inspectionProviders[typeName] = provider;
}

platformator::ScriptInspectionResult BehaviorFactoryRegistry::inspectFields(
    const std::string &typeName, const Behavior &instance, platformator::ScriptInspectionLimits limits) const
{
    const auto provider = inspectionProviders.find(typeName);
    if (provider == inspectionProviders.end())
        return {};
    return provider->second(instance, platformator::bounded_script_inspection_limits(limits));
}

Behavior *BehaviorFactoryRegistry::createBehavior(const std::string &typeName) const
{
    auto it = factories.find(typeName);
    if (it != factories.end())
    {
        return it->second();
    }
    return nullptr;
}

std::vector<platformator::ScriptDescriptor> BehaviorFactoryRegistry::registeredDescriptors() const
{
    std::vector<std::string> names;
    names.reserve(factories.size());
    for (const auto &[name, factory] : factories)
    {
        (void)factory;
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());

    std::vector<platformator::ScriptDescriptor> descriptors;
    descriptors.reserve(names.size());
    for (const auto &name : names)
    {
        const auto providers = descriptorProviders.find(name);
        if (providers == descriptorProviders.end() || providers->second.empty())
        {
            descriptors.push_back(platformator::ScriptDescriptor{name, false, {}});
            continue;
        }
        auto descriptor = providers->second.front()();
        descriptor.name = name;
        descriptor.fieldSchemaAvailable = true;
        const auto expected = platformator::script_descriptor_json(descriptor);
        for (std::size_t index = 1; index < providers->second.size(); ++index)
        {
            auto duplicate = providers->second[index]();
            duplicate.name = name;
            duplicate.fieldSchemaAvailable = true;
            if (platformator::script_descriptor_json(duplicate) != expected)
                throw std::runtime_error("inconsistent behavior descriptors registered for '" + name + "'");
        }
        descriptors.push_back(std::move(descriptor));
    }
    return descriptors;
}
