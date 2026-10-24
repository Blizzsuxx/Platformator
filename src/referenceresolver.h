#pragma once

#include "platformator/referenceresolver.h"
#include "platformator/runtime.h"
#include "pathmanager.h"

namespace platformator_detail
{
class ResourceDirectoryResolver : public platformator::ReferenceResolver
{
  public:
    ResourceDirectoryResolver(platformator::ReferenceResolver &base, std::filesystem::path directory)
        : base(base), directory(std::move(directory))
    {
    }
    BaseObject *findObject(int id, ObjectKind kind) override
    {
        return base.findObject(id, kind);
    }
    BaseObject *findHandle(platformator::ObjectHandle handle, ObjectKind kind) override
    {
        return base.findHandle(handle, kind);
    }
    TextureWrapper *loadTexture(const std::string &path) override
    {
        return base.loadTexture(
            PathManager::getInstance().resolveAssetPath(path, AssetPathType::Texture, true, directory).canonicalPath);
    }
    AudioWrapper *loadAudio(const std::string &path) override
    {
        return base.loadAudio(
            PathManager::getInstance().resolveAssetPath(path, AssetPathType::Audio, true, directory).canonicalPath);
    }
    AnimationClip *loadAnimationClip(const std::string &path) override
    {
        return base.loadAnimationClip(PathManager::getInstance()
                                          .resolveAssetPath(path, AssetPathType::AnimationClip, true, directory)
                                          .canonicalPath);
    }

  private:
    platformator::ReferenceResolver &base;
    std::filesystem::path directory;
};
class ActiveReferenceResolver : public platformator::ReferenceResolver
{
  public:
    explicit ActiveReferenceResolver(platformator::Runtime &runtime, std::filesystem::path directory = {})
        : runtime(runtime), directory(std::move(directory))
    {
    }
    BaseObject *findObject(int id, ObjectKind kind) override;
    BaseObject *findHandle(platformator::ObjectHandle handle, ObjectKind kind) override;
    TextureWrapper *loadTexture(const std::string &path) override;
    AudioWrapper *loadAudio(const std::string &path) override;
    AnimationClip *loadAnimationClip(const std::string &path) override;

  private:
    platformator::Runtime &runtime;
    std::filesystem::path directory;
};
} // namespace platformator_detail
