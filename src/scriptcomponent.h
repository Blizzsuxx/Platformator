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
    ScriptComponent();
    ScriptComponent(GameObject *gameObject);
    ~ScriptComponent() override;

    Behavior *addBehavior(Behavior *behavior);

    const std::vector<Behavior *> &getBehaviors() const;
    template <typename T>
    T *getBehavior() const;

    void fixedUpdate(double timeDelta);
    void update(double timeDelta);
    void lateUpdate(double timeDelta);
    void setGameObject(GameObject *gameObject) override;

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

void to_json(nlohmann::json &j, const ScriptComponent &scriptComponent);
void from_json(const nlohmann::json &j, ScriptComponent &scriptComponent);

template <typename T>
T *ScriptComponent::getBehavior() const
{
    for (Behavior *behavior : behaviors)
    {
        if (T *castedBehavior = dynamic_cast<T *>(behavior))
        {
            return castedBehavior;
        }
    }

    return nullptr;
}