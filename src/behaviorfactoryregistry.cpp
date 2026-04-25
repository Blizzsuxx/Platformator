#include "behaviorfactoryregistry.h"

#include "scriptcomponent.h"

void BehaviorFactoryRegistry::registerFactory(const std::string &type, Factory factory)
{
    factories[type] = std::move(factory);
}

Behavior *BehaviorFactoryRegistry::createBehavior(ScriptComponent *scriptComponent, const ScriptDescriptor &descriptor) const
{
    auto it = factories.find(descriptor.type);
    if (it == factories.end())
    {
        return nullptr;
    }

    Behavior *behavior = it->second();
    behavior->deserialize(descriptor);
    return scriptComponent->addBehavior(behavior);
}