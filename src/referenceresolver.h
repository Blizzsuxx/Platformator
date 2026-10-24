#pragma once

#include "platformator/referenceresolver.h"
#include "platformator/runtime.h"

namespace platformator_detail
{
class ActiveReferenceResolver : public platformator::ReferenceResolver
{
  public:
    explicit ActiveReferenceResolver(platformator::Runtime &runtime) : runtime(runtime)
    {
    }
    BaseObject *findObject(int id, ObjectKind kind) override;
    BaseObject *findHandle(platformator::ObjectHandle handle, ObjectKind kind) override;
    TextureWrapper *loadTexture(const std::string &path) override;
    AudioWrapper *loadAudio(const std::string &path) override;
    AnimationClip *loadAnimationClip(const std::string &path) override;

  private:
    platformator::Runtime &runtime;
};
} // namespace platformator_detail
