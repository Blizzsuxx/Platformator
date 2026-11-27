#pragma once

#include <string>
#include <vector>

#include "platformator/baseobject.h"
#include "platformator/referenceresolver.h"
#include "platformator/runtime.h"

class GameManager;

namespace platformator_detail
{
class RuntimeReferenceResolver : public platformator::ReferenceResolver
{
  public:
    explicit RuntimeReferenceResolver(GameManager &manager) noexcept : manager(manager)
    {
    }
    TextureWrapper *loadTexture(const std::string &path) override;
    AudioWrapper *loadAudio(const std::string &path) override;
    AnimationClip *loadAnimationClip(const std::string &path) override;
    Template *loadTemplate(const std::string &path) override;

  protected:
    GameManager &manager;
};

class ActiveReferenceResolver : public RuntimeReferenceResolver
{
  public:
    explicit ActiveReferenceResolver(platformator::Runtime &runtime);
    BaseObject *findObject(int id, ObjectKind kind) override;
    BaseObject *findHandle(platformator::ObjectHandle handle, ObjectKind kind) override;

  private:
    platformator::Runtime &runtime;
};

struct AssetRetentionLedger
{
    AssetRetentionLedger();
    ~AssetRetentionLedger();
    AssetRetentionLedger(AssetRetentionLedger &&) noexcept;
    AssetRetentionLedger &operator=(AssetRetentionLedger &&) noexcept;

    std::vector<Asset::Reference<TextureWrapper>> textures;
    std::vector<Asset::Reference<AudioWrapper>> audio;
    std::vector<Asset::Reference<AnimationClip>> clips;
    std::vector<Asset::Reference<Template>> templates;

    TextureWrapper *retain(TextureWrapper *asset);
    AudioWrapper *retain(AudioWrapper *asset);
    AnimationClip *retain(AnimationClip *asset);
    Template *retain(Template *asset);
};

class RetainingReferenceResolver : public RuntimeReferenceResolver
{
  public:
    RetainingReferenceResolver(GameManager &manager, AssetRetentionLedger &retainedAssets) noexcept
        : RuntimeReferenceResolver(manager), retainedAssets(retainedAssets)
    {
    }
    TextureWrapper *loadTexture(const std::string &path) override;
    AudioWrapper *loadAudio(const std::string &path) override;
    AnimationClip *loadAnimationClip(const std::string &path) override;
    Template *loadTemplate(const std::string &path) override;

  protected:
    AssetRetentionLedger &retainedAssets;
};
} // namespace platformator_detail
