#pragma once

#include "gameobject.h"
#include "scriptdescriptor.h"

class Collider;
class ScriptComponent;

class Behavior
{
    friend class ScriptComponent;
    friend class BehaviorFactoryRegistry;

public:
    Behavior();
    virtual ~Behavior() = default;

    GameObject *getGameObject() const;
    bool getEnabled() const;
    void setEnabled(bool enabled);
    virtual std::string getTypeName() const;
    virtual void deserialize(const ScriptDescriptor &descriptor);
    virtual void serialize(ScriptDescriptor &descriptor) const;

    virtual void start();
    virtual void update(double timeDelta);
    virtual void fixedUpdate(double timeDelta);
    virtual void lateUpdate(double timeDelta);
    virtual void onDestroy();
    virtual void onCollisionEnter(Collider *other, double timeDelta);
    virtual void onCollisionExit(Collider *other, double timeDelta);
    virtual void onCollisionStay(Collider *other, double timeDelta);

private:
    GameObject *gameObject;
    bool enabled;
};