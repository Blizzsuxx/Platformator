#include "behavior.h"

#include <filesystem>

#include "animationasset.h"
#include "gamemanager.h"
#include "scriptcomponent.h"
#include "collision.h"

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

GameObject *platformator_behavior_detail::findGameObjectByName(const std::string &name)
{
    if (name.empty())
    {
        return nullptr;
    }

    return GameManager::getInstance().getGameObject(name);
}

std::string platformator_behavior_detail::resolveAssetPath(const std::string &sourcePath, const std::string &path)
{
    if (path.empty())
    {
        return "";
    }

    const std::filesystem::path assetPath(path);
    if (assetPath.is_absolute() || sourcePath.empty())
    {
        return assetPath.lexically_normal().string();
    }

    return (std::filesystem::path(sourcePath).parent_path() / assetPath).lexically_normal().string();
}

AudioWrapper *platformator_behavior_detail::resolveAudioAssetByPath(const std::string &resolvedPath)
{
    return resolvedPath.empty() ? nullptr : GameManager::getInstance().loadAudio(resolvedPath);
}

TextureWrapper *platformator_behavior_detail::resolveTextureAssetByPath(const std::string &resolvedPath)
{
    return resolvedPath.empty() ? nullptr : GameManager::getInstance().loadTexture(resolvedPath);
}

AnimationSetAsset *platformator_behavior_detail::resolveAnimationSetAssetByPath(const std::string &resolvedPath)
{
    return resolvedPath.empty() ? nullptr : GameManager::getInstance().loadAnimationSet(resolvedPath);
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

void Behavior::onCollisionEnter(const Collision *, Collider *, double)
{
}

void Behavior::onCollisionExit(Collider *, double)
{
}

void Behavior::onCollisionStay(const Collision *, Collider *, double)
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

void Behavior::resolveFieldBindings()
{
    const std::vector<platformator_behavior_detail::BehaviorFieldDescriptor> &fields = getBehaviorFieldDescriptors();
    for (const platformator_behavior_detail::BehaviorFieldDescriptor &field : fields)
    {
        field.resolve(*this);
    }
}