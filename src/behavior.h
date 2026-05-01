#pragma once

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "assetreference.h"
#include "gameobject.h"
#include "objectreference.h"

class Collision;
class Collider;

class Behavior
{
    friend class ScriptComponent;
    friend class BehaviorFactoryRegistry;
    friend class GameManager;

public:
    Behavior();
    virtual ~Behavior() = default;

    GameObject *getGameObject() const;
    void setGameObject(GameObject *gameObject);

    virtual void start();
    virtual void update(double timeDelta);
    virtual void fixedUpdate(double timeDelta);
    virtual void lateUpdate(double timeDelta);
    virtual void onDestroy();
    virtual void onCollisionEnter(const Collision *collision, Collider *other, double timeDelta);
    virtual void onCollisionExit(Collider *other, double timeDelta);
    virtual void onCollisionStay(const Collision *collision, Collider *other, double timeDelta);

    virtual std::string getTypeName() const = 0;
    virtual void serialize(nlohmann::json &j) const = 0;
    virtual void deserialize(const nlohmann::json &j) = 0;
    virtual void resolveReferences() = 0;

private:
    GameObject *gameObject = nullptr;
};
