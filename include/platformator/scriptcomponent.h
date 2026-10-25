#pragma once

#include <cstddef>
#include <type_traits>
#include <vector>

#include <json.hpp>

#include "platformator/behavior.h"

class Collision;
class Collider;

class ScriptComponent : public Component
{
    friend class Collider;
    friend class GameManager;

  public:
    ScriptComponent();
    ScriptComponent(GameObject *gameObject);
    ~ScriptComponent() override;

    Behavior *addBehavior(Behavior *behavior);

    const std::vector<Behavior *> &getBehaviors() const;
    const std::vector<Behavior *> &getPendingBehaviors() const
    {
        return pendingBehaviors;
    }

    template <typename T> T *getBehavior() const;

    void fixedUpdate(double timeDelta);
    void update(double timeDelta);
    void lateUpdate(double timeDelta);
    void setGameObject(GameObject *gameObject) override;

    size_t getGameManagerIndex() const;
    void setGameManagerIndex(size_t index);
    bool canDispatch() const;

  private:
    void startPendingBehaviors(platformator::ReferenceResolver &resolver);
    std::vector<Behavior *> behaviors;
    std::vector<Behavior *> pendingBehaviors;
    size_t pendingStartIndex = SIZE_MAX; // Global script startup queue.
    size_t gameManagerIndex;
    bool destructionDelivered = false;

    void dispatchCollisionEnter(const Collision *collision, Collider *other, double timeDelta);
    void dispatchCollisionExit(Collider *other, double timeDelta);
};

template <> struct ComponentTypeFor<ScriptComponent>
{
    static constexpr ComponentType value = ComponentType::SCRIPT;
};

void to_json(nlohmann::json &j, const ScriptComponent &scriptComponent);
void from_json(const nlohmann::json &j, ScriptComponent &scriptComponent);

template <typename T> T *ScriptComponent::getBehavior() const
{
    static_assert(std::is_base_of_v<Behavior, T>, "getBehavior<T>() requires T to derive from Behavior");

    for (Behavior *behavior : behaviors)
        if (auto *castedBehavior = dynamic_cast<T *>(behavior))
            return castedBehavior;
    for (Behavior *behavior : pendingBehaviors)
        if (auto *castedBehavior = dynamic_cast<T *>(behavior))
            return castedBehavior;

    return nullptr;
}
