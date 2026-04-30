#include "behaviorfactoryregistry.h"
#include "behavior.h"

void BehaviorFactoryRegistry::registerFactory(const std::string &typeName, std::function<Behavior *()> factory)
{
    factories[typeName] = std::move(factory);
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
