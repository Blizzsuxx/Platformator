#include "platformator/assetreference.h"
#include "platformator/objectreference.h"
#include "referenceserialization.h"
#include "platformator/gameobject.h"

#include "platformator/runtime.h"

namespace platformator_detail
{
namespace
{
thread_local const std::unordered_set<const BaseObject *> *savingObjects = nullptr;
}
SceneReferenceScope::SceneReferenceScope(const std::vector<GameObject *> &roots) : previous(savingObjects)
{
    for (const auto *root : roots)
    {
        if (!root)
            throw std::invalid_argument("Cannot save a null scene root");
        add(*root);
    }
    savingObjects = &objects;
}
SceneReferenceScope::~SceneReferenceScope()
{
    savingObjects = previous;
}
void SceneReferenceScope::add(const GameObject &object)
{
    auto &runtime = platformator::Runtime::current();
    if (runtime.resolveHandle(object.getHandle(), ObjectKind::GameObject) != &object || !objects.insert(&object).second)
        throw std::runtime_error("Saved graph contains an unpublished, foreign, or duplicate object");
    for (size_t i = 0; i < COMPONENT_TYPE_COUNT; ++i)
        if (auto *component = object.getComponents()[i])
            objects.insert(component);
    for (const auto *child : object.getChildren())
        add(*child);
}
void validateSceneReference(const BaseObject *object)
{
    if (savingObjects && !savingObjects->contains(object))
        throw std::runtime_error("Live reference to file ID " + std::to_string(object->getId()) +
                                 " is outside the saved scene");
}
TextureWrapper *loadTextureAssetReference(platformator::Runtime &runtime, const std::string &assetPath)
{
    return runtime.loadTexture(assetPath);
}

AudioWrapper *loadAudioAssetReference(platformator::Runtime &runtime, const std::string &assetPath)
{
    return runtime.loadAudio(assetPath);
}

AnimationClip *loadAnimationClipAssetReference(platformator::Runtime &runtime, const std::string &assetPath)
{
    return runtime.loadAnimationClip(assetPath);
}

BaseObject *resolveObjectReference(platformator::Runtime &runtime, int objectId)
{
    return runtime.getObjectById(objectId);
}
} // namespace platformator_detail
