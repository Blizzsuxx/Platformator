#include "referenceresolver.h"
#include "platformator/baseobject.h"

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
    return runtime.loadTexture(path);
}
AudioWrapper *ActiveReferenceResolver::loadAudio(const std::string &path)
{
    return runtime.loadAudio(path);
}
AnimationClip *ActiveReferenceResolver::loadAnimationClip(const std::string &path)
{
    return runtime.loadAnimationClip(path);
}
} // namespace platformator_detail
