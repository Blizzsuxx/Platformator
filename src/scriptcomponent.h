#pragma once

#include <memory>
#include <type_traits>
#include <vector>

#include "behavior.h"

class Collider;

class ScriptComponent : public Component
{
    friend class Collider;

public:
    ScriptComponent(GameObject *gameObject);
    ~ScriptComponent() override;

    Behavior *addBehavior(Behavior *behavior);

    const std::vector<Behavior *> &getBehaviors() const;

    void fixedUpdate(double timeDelta);
    void update(double timeDelta);
    void lateUpdate(double timeDelta);

    size_t getGameManagerIndex() const;
    void setGameManagerIndex(size_t index);

private:
    std::vector<Behavior *> behaviors;
    size_t gameManagerIndex;

    void destroyBehaviors();
    void dispatchCollisionEnter(Collider *other, double timeDelta);
    void dispatchCollisionExit(Collider *other, double timeDelta);
    void dispatchCollisionStay(Collider *other, double timeDelta);
};

template <>
struct ComponentTypeFor<ScriptComponent>
{
    static constexpr ComponentType value = ComponentType::SCRIPT;
};