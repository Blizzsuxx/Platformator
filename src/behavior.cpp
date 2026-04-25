#include "behavior.h"

#include "scriptcomponent.h"

Behavior::Behavior()
    : gameObject(nullptr), enabled(true)
{
}

namespace
{
    const std::vector<platformator_behavior_detail::BehaviorFieldDescriptor> &emptyBehaviorFieldDescriptors()
    {
        static const std::vector<platformator_behavior_detail::BehaviorFieldDescriptor> descriptors;
        return descriptors;
    }
}

GameObject *Behavior::getGameObject() const
{
    return gameObject;
}

bool Behavior::getEnabled() const
{
    return enabled;
}

std::string Behavior::getTypeName() const
{
    return "";
}

void Behavior::deserialize(const ScriptDescriptor &descriptor)
{
    const std::vector<platformator_behavior_detail::BehaviorFieldDescriptor> &fields = getBehaviorFieldDescriptors();
    for (const platformator_behavior_detail::BehaviorFieldDescriptor &field : fields)
    {
        field.deserialize(*this, field.name, descriptor);
    }
}

void Behavior::serialize(ScriptDescriptor &descriptor) const
{
    const std::vector<platformator_behavior_detail::BehaviorFieldDescriptor> &fields = getBehaviorFieldDescriptors();
    for (const platformator_behavior_detail::BehaviorFieldDescriptor &field : fields)
    {
        field.serialize(*this, field.name, descriptor);
    }
}

void Behavior::setEnabled(bool enabled)
{
    this->enabled = enabled;
}

void Behavior::start()
{
}

void Behavior::update(double)
{
}

void Behavior::fixedUpdate(double)
{
}

void Behavior::lateUpdate(double)
{
}

void Behavior::onDestroy()
{
}

void Behavior::onCollisionEnter(Collider *, double)
{
}

void Behavior::onCollisionExit(Collider *, double)
{
}

void Behavior::onCollisionStay(Collider *, double)
{
}

void Behavior::setGameObject(GameObject *gameObject)
{
    this->gameObject = gameObject;
}

GameObject *Behavior::getGameObject()
{
    return gameObject;
}

const std::vector<platformator_behavior_detail::BehaviorFieldDescriptor> &Behavior::getBehaviorFieldDescriptors() const
{
    return emptyBehaviorFieldDescriptors();
}