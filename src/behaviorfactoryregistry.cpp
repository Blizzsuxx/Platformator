#include "behaviorfactoryregistry.h"

#include "scriptcomponent.h"

void BehaviorFactoryRegistry::registerFactory(const std::string &type, Factory factory)
{
    factories[type] = std::move(factory);
}

Behavior *BehaviorFactoryRegistry::instantiateBehavior(ScriptComponent *scriptComponent, const BehaviorSpec &spec) const
{
    auto it = factories.find(spec.type);
    if (it == factories.end())
    {
        return nullptr;
    }

    Behavior *behavior = it->second();
    behavior->setEnabled(spec.enabled);
    behavior->deserialize(spec);
    return scriptComponent->addBehavior(behavior);
}