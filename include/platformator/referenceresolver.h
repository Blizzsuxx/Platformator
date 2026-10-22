#pragma once

#include <string>
#include "platformator/objecthandle.h"

class TextureWrapper;
class AudioWrapper;
class AnimationClip;

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

    template <typename T> void resolve(T &field)
    {
        if constexpr (requires { field.resolve(*this); })
            field.resolve(*this);
    }
};
} // namespace platformator
