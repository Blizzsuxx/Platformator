#include "referenceresolver.h"
#include "platformator/baseobject.h"
#include "gamemanager.h"
#include "runtimeaccess.h"

namespace platformator_detail
{
BaseObject *ActiveReferenceResolver::findObject(int id, ObjectKind kind)
{
    auto *object = runtime.getObjectById(id);
    return object ? runtime.resolveHandle(object->getHandle(), kind) : nullptr;
}
BaseObject *ActiveReferenceResolver::findHandle(platformator::ObjectHandle handle, ObjectKind kind)
{
    return runtime.resolveHandle(handle, kind);
}
TextureWrapper *ActiveReferenceResolver::loadTexture(const std::string &path)
{
    const auto resolved = PathManager::getInstance().resolveAssetPath(path, AssetPathType::Texture, true, directory);
    return RuntimeAccess::gameManager().loadTexture(resolved.canonicalPath, true);
}
AudioWrapper *ActiveReferenceResolver::loadAudio(const std::string &path)
{
    const auto resolved = PathManager::getInstance().resolveAssetPath(path, AssetPathType::Audio, true, directory);
    return RuntimeAccess::gameManager().loadAudio(resolved.canonicalPath, true);
}
AnimationClip *ActiveReferenceResolver::loadAnimationClip(const std::string &path)
{
    const auto resolved = PathManager::getInstance().resolveAssetPath(path, AssetPathType::AnimationClip, true, directory);
    return RuntimeAccess::gameManager().loadAnimationClip(resolved.canonicalPath, *this, true);
}
} // namespace platformator_detail
