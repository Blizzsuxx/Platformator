#include "platformator/scriptregistration.h"

#include "behaviorfactoryregistry.h"

#include <utility>

namespace platformator
{
    void registerScriptFactory(std::string_view typeName, Behavior *(*factory)())
    {
        BehaviorFactoryRegistry::getInstance().registerFactory(std::string(typeName), factory);
    }

    void registerScriptDescriptorProvider(std::string_view typeName, ScriptDescriptorProvider provider)
    {
        BehaviorFactoryRegistry::getInstance().registerDescriptorProvider(std::string(typeName), provider);
    }

    Behavior *createRegisteredBehavior(std::string_view typeName)
    {
        return BehaviorFactoryRegistry::getInstance().createBehavior(std::string(typeName));
    }

    nlohmann::json exportRegisteredBehaviorCatalog()
    {
        nlohmann::json behaviors = nlohmann::json::array();
        for (const auto &descriptor : BehaviorFactoryRegistry::getInstance().registeredDescriptors())
            behaviors.push_back(script_descriptor_json(descriptor));
        return {{"schema_version", 1}, {"behaviors", std::move(behaviors)}};
    }
} // namespace platformator
