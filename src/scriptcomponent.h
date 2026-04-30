#pragma once

#include <memory>
#include <type_traits>
#include <vector>

#include "behavior.h"

#include <json.hpp>
#include "jsonhelpers.h"
#include "behaviorfactoryregistry.h"

class Collider;

class ScriptComponent : public Component
{
    friend class Collider;

public:
    ScriptComponent(GameObject *gameObject);
    ~ScriptComponent() override;

    Behavior *addBehavior(Behavior *behavior);

    const std::vector<Behavior *> &getBehaviors() const;
    template <typename T>
    T *getBehavior() const;

    void fixedUpdate(double timeDelta);
    void update(double timeDelta);
    void lateUpdate(double timeDelta);

    size_t getGameManagerIndex() const;
    void setGameManagerIndex(size_t index);

private:
    std::vector<Behavior *> behaviors;
    size_t gameManagerIndex;

    void destroyBehaviors();
    void dispatchCollisionEnter(const Collision *collision, Collider *other, double timeDelta);
    void dispatchCollisionExit(Collider *other, double timeDelta);
    void dispatchCollisionStay(const Collision *collision, Collider *other, double timeDelta);
};

template <>
struct ComponentTypeFor<ScriptComponent>
{
    static constexpr ComponentType value = ComponentType::SCRIPT;
};

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
        if (behavior)
        {
            behavior->deserialize(behaviorJson);
            scriptComponent.addBehavior(behavior);
        }
    }
}