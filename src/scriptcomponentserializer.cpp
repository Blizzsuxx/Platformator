#include "scriptcomponent.h"

void to_json(nlohmann::json &j, const ScriptComponent &scriptComponent)
{
    j["behaviors"] = nlohmann::json::array();
    for (const auto &behavior : scriptComponent.getBehaviors())
    {
        nlohmann::json behaviorJson;
        behavior->serialize(behaviorJson);
        j["behaviors"].push_back(behaviorJson);
    }
}

void from_json(const nlohmann::json &j, ScriptComponent &scriptComponent)
{
    for (const auto &behaviorJson : j.at("behaviors"))
    {
        std::string type = behaviorJson.at("type").get<std::string>();
        Behavior *behavior = BehaviorFactoryRegistry::getInstance().createBehavior(type);
        if (behavior != nullptr)
        {
            behavior->deserialize(behaviorJson);
            scriptComponent.addBehavior(behavior);
        }
    }
}