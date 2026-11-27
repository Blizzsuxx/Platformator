#include "scene/referenceresolver.h"

#include "core/gamemanager.h"
#include "core/runtimeaccess.h"
#include "platformator/animationclip.h"
#include "platformator/audiowrapper.h"
#include "platformator/baseobject.h"
#include "platformator/template.h"
#include "platformator/texturewrapper.h"

namespace platformator_detail
{
TextureWrapper *RuntimeReferenceResolver::loadTexture(const std::string &path)
{
    return manager.assetStore().loadTexture(path, true);
}

AudioWrapper *RuntimeReferenceResolver::loadAudio(const std::string &path)
{
    return manager.assetStore().loadAudio(path, true);
}

AnimationClip *RuntimeReferenceResolver::loadAnimationClip(const std::string &path)
{
    return manager.assetStore().loadAnimationClip(path, *this, true);
}

Template *RuntimeReferenceResolver::loadTemplate(const std::string &path)
{
    return manager.assetStore().loadTemplate(path, true);
}

ActiveReferenceResolver::ActiveReferenceResolver(platformator::Runtime &runtime)
    : RuntimeReferenceResolver(RuntimeAccess::gameManager()), runtime(runtime)
{
}

BaseObject *ActiveReferenceResolver::findObject(int id, ObjectKind kind)
{
    auto *object = runtime.getObjectById(id);
    return object ? runtime.resolveHandle(object->getHandle(), kind) : nullptr;
}

BaseObject *ActiveReferenceResolver::findHandle(platformator::ObjectHandle handle, ObjectKind kind)
{
    return runtime.resolveHandle(handle, kind);
}

AssetRetentionLedger::AssetRetentionLedger() = default;
AssetRetentionLedger::~AssetRetentionLedger() = default;
AssetRetentionLedger::AssetRetentionLedger(AssetRetentionLedger &&) noexcept = default;
AssetRetentionLedger &AssetRetentionLedger::operator=(AssetRetentionLedger &&) noexcept = default;

TextureWrapper *AssetRetentionLedger::retain(TextureWrapper *asset)
{
    textures.push_back(Asset::retain(asset));
    return asset;
}

AudioWrapper *AssetRetentionLedger::retain(AudioWrapper *asset)
{
    audio.push_back(Asset::retain(asset));
    return asset;
}

AnimationClip *AssetRetentionLedger::retain(AnimationClip *asset)
{
    clips.push_back(Asset::retain(asset));
    return asset;
}

Template *AssetRetentionLedger::retain(Template *asset)
{
    templates.push_back(Asset::retain(asset));
    return asset;
}
} // namespace platformator_detail
