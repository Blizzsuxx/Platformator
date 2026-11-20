#pragma once

#include <string>
#include "platformator/objecthandle.h"

class TextureWrapper;
class AudioWrapper;
struct AnimationClip;
class Template;

namespace platformator
{
class ReferenceResolver
{
  public:
    virtual ~ReferenceResolver() = default;
    virtual BaseObject *findObject(int id, platformator_detail::ObjectKind kind) = 0;
    virtual BaseObject *findHandle(ObjectHandle handle, platformator_detail::ObjectKind kind) = 0;
    virtual TextureWrapper *loadTexture(const std::string &path) = 0;
    virtual AudioWrapper *loadAudio(const std::string &path) = 0;
    virtual AnimationClip *loadAnimationClip(const std::string &path) = 0;
    virtual Template *loadTemplate(const std::string &path) = 0;
    virtual bool allowDeferredTemplate(const std::string &)
    {
        return false;
    }
    virtual bool allowDeferredInternalReferenceError() const
    {
        return false;
    }
    virtual bool shouldResolveObjectReferences() const
    {
        return true;
    }
    virtual bool isExternalObjectId(int) const
    {
        return false;
    }

    template <typename T> void resolve(T &field)
    {
        if constexpr (requires { field.resolve(*this); })
            field.resolve(*this);
    }
};
} // namespace platformator
