#pragma once

#include <string>

#include <json.hpp>

#include "platformator/assetreference.h"
#include "platformator/gameobject.h"
#include "platformator/objectreference.h"

class Collision;
class Collider;

namespace platformator
{
    class Runtime;
}

class Behavior
{
public:
    Behavior();
    virtual ~Behavior() = default;

    GameObject *getGameObject() const;
    platformator::Runtime &getRuntime() const;

    template <typename T>
    T *getBehavior(GameObject *gameObject) const;

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