#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <SDL3/SDL.h>
#include <json.hpp>

#include "assets/assetrequestaliases.h"
#include "platformator/animationclip.h"
#include "platformator/audiowrapper.h"
#include "platformator/template.h"
#include "platformator/texturewrapper.h"

class GameManager;

namespace platformator
{
class ReferenceResolver;
}

namespace platformator_detail
{
class AssetStore
{
  public:
    explicit AssetStore(GameManager &owner);

    TextureWrapper *loadTexture(const std::string &filePath, bool required = false);
    TextureWrapper *findTexture(const std::string &filePath);
    std::uint64_t textureCacheBytes() const noexcept;
    TextureWrapper *reloadTexture(const std::string &filePath, SDL_Surface *preparedSurface);
    void freeTexture(TextureWrapper *textureWrapper);

    AudioWrapper *loadAudio(const std::string &filePath, bool required = false);
    void freeAudio(AudioWrapper *audioWrapper);

    AnimationClip *loadAnimationClip(const std::string &filePath);
    AnimationClip *loadAnimationClip(const std::string &filePath, platformator::ReferenceResolver &resolver,
                                     bool required);
    AnimationClip *findAnimationClip(const std::string &filePath);
    AnimationClip *reloadAnimationClip(const std::string &filePath, const nlohmann::json &preparedDocument);
    void freeAnimationClip(AnimationClip *animationClip);

    Template *loadTemplate(const std::string &filePath, bool required = false, bool *deferredCyclic = nullptr);
    void freeTemplate(Template *tpl);

    void freeAll();

  private:
    template <typename Resource> struct Cache
    {
        std::unordered_map<std::string, std::unique_ptr<Resource>> entries;
        AssetRequestAliases aliases;
        std::vector<std::unique_ptr<Resource>> retired;

        Resource *lookupAlias(const std::string &request) const;
        Resource *lookupCanonical(const std::string &request, const std::string &canonical);
        Resource *find(const std::string &request);
        Resource *publish(const std::string &request, const std::string &canonical,
                          std::unique_ptr<Resource> resource);
        Resource *replace(const std::string &request, const std::string &canonical,
                          std::unique_ptr<Resource> replacement);
        void free(Resource *resource);
        void clear();
    };

    template <typename Resource, typename BuildFn>
    Resource *loadCached(Cache<Resource> &cache, const std::string &filePath, const std::string &resolveRequest,
                         bool required, const char *emptyMessage, BuildFn &&build);
    template <typename Resource, typename BuildFn>
    Resource *reloadCached(Cache<Resource> &cache, const std::string &filePath, BuildFn &&build);

    GameManager &owner;
    Cache<TextureWrapper> textures;
    Cache<AudioWrapper> audio;
    Cache<AnimationClip> animations;
    Cache<Template> templates;
    std::unordered_set<std::string> activeTemplateLoads;
};
} // namespace platformator_detail
