#include "platformator/scriptregistration.h"

#include "behaviorfactoryregistry.h"

namespace platformator
{
    void registerScriptFactory(std::string_view typeName, Behavior *(*factory)())
    {
        BehaviorFactoryRegistry::getInstance().registerFactory(std::string(typeName), factory);
    }

    Behavior *createRegisteredBehavior(std::string_view typeName)
    {
        return BehaviorFactoryRegistry::getInstance().createBehavior(std::string(typeName));
    }
}