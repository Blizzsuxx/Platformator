#pragma once

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "gameobject.h"

class Behavior
{
    friend class ScriptComponent;
    friend class BehaviorFactoryRegistry;
    friend class GameManager;

public:
    Behavior();
    virtual ~Behavior() = default;

    GameObject *getGameObject() const;
    bool getEnabled() const;
    void setEnabled(bool enabled);
    virtual void deserialize(const BehaviorSpec &spec);
    virtual void serialize(BehaviorSpec &spec) const;

    virtual void start();
    virtual void update(double timeDelta);
    virtual void fixedUpdate(double timeDelta);
    virtual void lateUpdate(double timeDelta);
    virtual void onDestroy();
    virtual void onCollisionEnter(const Collision *collision, Collider *other, double timeDelta);
    virtual void onCollisionExit(Collider *other, double timeDelta);
    virtual void onCollisionStay(const Collision *collision, Collider *other, double timeDelta);

private:
    bool enabled;
};

#define SERIALIZ
