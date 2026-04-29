#include "behavior.h"

#include <filesystem>

#include "animationclip.h"
#include "collision.h"
#include "gamemanager.h"
#include "scriptcomponent.h"

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

GameObject *platformator_behavior_detail::findGameObjectById(uint64_t id)
{
    if (id == 0)
    {
        return nullptr;
    }

    return GameManager::getInstance().getGameObjectById(id);
}

Component *platformator_behavior_detail::findComponentById(uint64_t id)
{
    if (id == 0)
    {
        return nullptr;
    }

    return GameManager::getInstance().getComponentById(id);
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
    if (resolvedPath.empty())
    {
        return nullptr;
    }

    AudioWrapper *audio = GameManager::getInstance().loadAudio(resolvedPath);
    if (audio != nullptr && audio->getAudio() != nullptr)
    {
        return audio;
    }

    if (audio != nullptr)
    {
        GameManager::getInstance().freeAudio(audio);
    }

    return nullptr;
}

TextureWrapper *platformator_behavior_detail::resolveTextureAssetByPath(const std::string &resolvedPath)
{
    if (resolvedPath.empty())
    {
        return nullptr;
    }

    TextureWrapper *texture = GameManager::getInstance().loadTexture(resolvedPath);
    if (texture != nullptr && texture->getTexture() != nullptr)
    {
        return texture;
    }

    if (texture != nullptr)
    {
        GameManager::getInstance().freeTexture(texture);
    }

    return nullptr;
}

AnimationClip *platformator_behavior_detail::resolveAnimationClipByPath(const std::string &resolvedPath)
{
    if (resolvedPath.empty())
    {
        return nullptr;
    }

    return GameManager::getInstance().loadAnimationClip(resolvedPath);
}

void platformator_behavior_detail::GameObjectReference::resolve()
{
    gameObject = id == 0 ? nullptr : findGameObjectById(id);
}

Behavior::Behavior()
    : gameObject(nullptr), enabled(true), registeredTypeName()
{
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
    return registeredTypeName;
}

void Behavior::deserialize(const BehaviorSpec &spec)
{
    const std::vector<platformator_behavior_detail::BehaviorFieldDescriptor> &fields = getBehaviorFieldDescriptors();
    for (const platformator_behavior_detail::BehaviorFieldDescriptor &field : fields)
    {
        field.deserialize(*this, field.name, spec);
    }
}

void Behavior::serialize(BehaviorSpec &spec) const
{
    const std::vector<platformator_behavior_detail::BehaviorFieldDescriptor> &fields = getBehaviorFieldDescriptors();
    for (const platformator_behavior_detail::BehaviorFieldDescriptor &field : fields)
    {
        field.serialize(*this, field.name, spec);
    }
}

void Behavior::setEnabled(bool enabled)
{
    this->enabled = enabled;
}

void Behavior::setRegisteredTypeName(std::string typeName)
{
    registeredTypeName = std::move(typeName);
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
