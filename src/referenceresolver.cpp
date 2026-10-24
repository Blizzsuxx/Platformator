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
    return RuntimeAccess::gameManager().loadTexture(path, true);
}
AudioWrapper *ActiveReferenceResolver::loadAudio(const std::string &path)
{
    return RuntimeAccess::gameManager().loadAudio(path, true);
}
AnimationClip *ActiveReferenceResolver::loadAnimationClip(const std::string &path)
{
    return RuntimeAccess::gameManager().loadAnimationClip(path, *this, true);
}
} // namespace platformator_detail
