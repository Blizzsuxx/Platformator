#include "platformator/scriptcomponent.h"

#include "platformator/scriptregistration.h"

void to_json(nlohmann::json &j, const ScriptComponent &scriptComponent)
{
    j["id"] = scriptComponent.getId();
    j["behaviors"] = nlohmann::json::array();
    for (const auto &behavior : scriptComponent.getBehaviors())
    {
        nlohmann::json behaviorJson;
        behavior->serialize(behaviorJson);
        j["behaviors"].push_back(behaviorJson);
    }
    j["type"] = ComponentType::SCRIPT;
}

void from_json(const nlohmann::json &j, ScriptComponent &scriptComponent)
{
    scriptComponent.setId(j.at("id").get<int>());

    for (const auto &behaviorJson : j.at("behaviors"))
    {
        std::string type = behaviorJson.at("type").get<std::string>();
        std::unique_ptr<Behavior> behavior(platformator::createRegisteredBehavior(type));
        if (!behavior)
            throw std::runtime_error("Unknown behavior type: " + type);
        behavior->deserialize(behaviorJson);
        scriptComponent.addBehavior(behavior.get());
        behavior.release();
    }
}