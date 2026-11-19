#include "assetstore.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <unordered_set>

#include <SDL3_image/SDL_image.h>
#include <SDL3_mixer/SDL_mixer.h>

#include "constants.h"
#include "gamemanager.h"
#include "pathmanager.h"
#include "platformator/runtime.h"
#include "referenceresolver.h"
#include "scenecomponentio.h"
#include "sdlwindow.h"

namespace platformator_detail
{
template <typename Resource> Resource *AssetStore::Cache<Resource>::lookupAlias(const std::string &request) const
{
    if (const auto *canonical = aliases.find(request))
    {
        const auto found = entries.find(*canonical);
        if (found != entries.end())
        {
            return found->second.get();
        }
        throw std::logic_error("Stale asset request alias");
    }
    return nullptr;
}

template <typename Resource>
Resource *AssetStore::Cache<Resource>::lookupCanonical(const std::string &request, const std::string &canonical)
{
    const auto found = entries.find(canonical);
    if (found == entries.end())
    {
        return nullptr;
    }
    aliases.add(request, canonical);
    return found->second.get();
}

template <typename Resource> Resource *AssetStore::Cache<Resource>::find(const std::string &request)
{
    if (request.empty())
    {
        return nullptr;
    }
    if (const auto *canonical = aliases.find(request))
    {
        const auto found = entries.find(*canonical);
        return found == entries.end() ? nullptr : found->second.get();
    }
    try
    {
        const auto canonical = PathManager::getInstance().canonicalizeAssetPath(request);
        return lookupCanonical(request, canonical);
    }
    catch (...)
    {
        return nullptr;
    }
}

template <typename Resource>
Resource *AssetStore::Cache<Resource>::publish(const std::string &request, const std::string &canonical,
                                               std::unique_ptr<Resource> resource)
{
    return publishAsset(entries, aliases, request, canonical, std::move(resource));
}

template <typename Resource>
Resource *AssetStore::Cache<Resource>::replace(const std::string &request, const std::string &canonical,
                                               std::unique_ptr<Resource> replacement)
{
    const auto found = entries.find(canonical);
    if (found == entries.end())
    {
        Resource *published = publishAsset(entries, aliases, request, canonical, std::move(replacement));
        aliases.rebind(request, canonical);
        return published;
    }
    if (found->second->getReferenceCount() != 0)
    {
        retired.reserve(retired.size() + 1);
    }
    aliases.rebind(request, canonical);
    if (found->second->getReferenceCount() != 0)
    {
        retired.push_back(std::move(found->second));
    }
    else
    {
        found->second.reset();
    }
    found->second = std::move(replacement);
    return found->second.get();
}

template <typename Resource> void AssetStore::Cache<Resource>::free(Resource *resource)
{
    if (resource == nullptr)
    {
        return;
    }
    const auto it = entries.find(resource->getFilePath());
    if (it != entries.end() && it->second.get() == resource)
    {
        aliases.remove(it->first);
        entries.erase(it);
        return;
    }
    std::erase_if(retired, [&](const auto &entry) { return entry.get() == resource; });
}

template <typename Resource> void AssetStore::Cache<Resource>::clear()
{
    aliases.clear();
    // Move to locals before clearing so re-entrant free() calls during nested
    // resource destruction (e.g. Template or AnimationClip) never invalidate iterators.
    auto cached = std::move(entries);
    auto retiredLocal = std::move(retired);
    cached.clear();
    retiredLocal.clear();
}

AssetStore::AssetStore(GameManager &owner) : owner(owner)
{
}

TextureWrapper *AssetStore::loadTexture(const std::string &filePath, bool required)
{
    if (filePath.empty())
    {
        if (required)
        {
            throw std::invalid_argument("Required texture path is empty");
        }
        return nullptr;
    }
    if (auto *cached = textures.lookupAlias(filePath))
    {
        return cached;
    }
    const auto resolved = PathManager::getInstance().resolveAssetPath(filePath);
    if (auto *cached = textures.lookupCanonical(filePath, resolved.canonicalPath))
    {
        return cached;
    }
    if (!owner.getWindow()->getRenderer())
    {
        throw std::runtime_error("Textures require a video renderer: " + filePath);
    }
    std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> texture(
        IMG_LoadTexture(owner.getWindow()->getRenderer(), resolved.absolutePath.c_str()), SDL_DestroyTexture);
    if (!texture)
    {
        throw std::runtime_error("Texture '" + filePath + "': " + SDL_GetError());
    }
    auto wrapper = std::make_unique<TextureWrapper>(texture.get(), resolved.canonicalPath);
    texture.release();
    owner.tagAssetRuntime(*wrapper);
    return textures.publish(filePath, resolved.canonicalPath, std::move(wrapper));
}

TextureWrapper *AssetStore::findTexture(const std::string &filePath)
{
    return textures.find(filePath);
}

std::uint64_t AssetStore::textureCacheBytes() const noexcept
{
    std::uint64_t total = 0;
    std::unordered_set<const TextureWrapper *> seen;
    const auto account = [&](const TextureWrapper *wrapper) {
        if (wrapper == nullptr || !seen.insert(wrapper).second || wrapper->getTexture() == nullptr)
        {
            return true;
        }
        float width = 0.0f;
        float height = 0.0f;
        if (!SDL_GetTextureSize(wrapper->getTexture(), &width, &height) || width <= 0.0f || height <= 0.0f)
        {
            return true;
        }
        const auto pixels = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
        if (pixels > (std::numeric_limits<std::uint64_t>::max() - total) / 4ULL)
        {
            return false;
        }
        total += pixels * 4ULL;
        return true;
    };
    for (const auto &entry : textures.entries)
    {
        if (!account(entry.second.get()))
        {
            return std::numeric_limits<std::uint64_t>::max();
        }
    }
    for (const auto &entry : textures.retired)
    {
        if (!account(entry.get()))
        {
            return std::numeric_limits<std::uint64_t>::max();
        }
    }
    return total;
}

TextureWrapper *AssetStore::reloadTexture(const std::string &filePath, SDL_Surface *preparedSurface)
{
    owner.assertCanMutate();
    if (!owner.isIdle())
    {
        throw std::logic_error("asset reload requires an idle Runtime owner boundary");
    }
    if (filePath.empty() || preparedSurface == nullptr)
    {
        throw std::invalid_argument("Prepared texture reload requires a path and decoded surface");
    }
    const auto resolved = PathManager::getInstance().resolveAssetPath(filePath);
    SDL_Renderer *renderer = owner.getWindow()->getRenderer();
    if (!renderer)
    {
        throw std::runtime_error("Textures require a video renderer: " + filePath);
    }
    std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> texture(
        SDL_CreateTextureFromSurface(renderer, preparedSurface), SDL_DestroyTexture);
    if (!texture)
    {
        throw std::runtime_error("Texture '" + filePath + "': " + SDL_GetError());
    }
    auto replacement = std::make_unique<TextureWrapper>(texture.get(), resolved.canonicalPath);
    texture.release();
    owner.tagAssetRuntime(*replacement);
    return textures.replace(filePath, resolved.canonicalPath, std::move(replacement));
}

void AssetStore::freeTexture(TextureWrapper *textureWrapper)
{
    if (textureWrapper == nullptr)
    {
        return;
    }
    PLATFORMATOR_LOG("Freeing texture: %s\n", textureWrapper->getFilePath().c_str());
    textures.free(textureWrapper);
}

AudioWrapper *AssetStore::loadAudio(const std::string &filePath, bool required)
{
    if (filePath.empty())
    {
        if (required)
        {
            throw std::invalid_argument("Required audio path is empty");
        }
        return nullptr;
    }
    if (auto *cached = audio.lookupAlias(filePath))
    {
        return cached;
    }
    const auto resolved = PathManager::getInstance().resolveAssetPath(filePath);
    if (auto *cached = audio.lookupCanonical(filePath, resolved.canonicalPath))
    {
        return cached;
    }
    if (!owner.getWindow()->getMixer())
    {
        throw std::runtime_error("Audio assets require an audio backend: " + filePath);
    }
    std::unique_ptr<MIX_Audio, decltype(&MIX_DestroyAudio)> loaded(
        MIX_LoadAudio(owner.getWindow()->getMixer(), resolved.absolutePath.c_str(), true), MIX_DestroyAudio);
    if (!loaded)
    {
        throw std::runtime_error("Audio '" + filePath + "': " + SDL_GetError());
    }
    auto wrapper = std::make_unique<AudioWrapper>(loaded.get(), resolved.canonicalPath);
    loaded.release();
    owner.tagAssetRuntime(*wrapper);
    return audio.publish(filePath, resolved.canonicalPath, std::move(wrapper));
}

void AssetStore::freeAudio(AudioWrapper *audioWrapper)
{
    if (audioWrapper == nullptr)
    {
        return;
    }
    PLATFORMATOR_LOG("Freeing audio: %s\n", audioWrapper->getFilePath().c_str());
    audio.free(audioWrapper);
}

AnimationClip *AssetStore::loadAnimationClip(const std::string &filePath)
{
    ActiveReferenceResolver resolver(platformator::Runtime::current());
    return loadAnimationClip(filePath, resolver, false);
}

AnimationClip *AssetStore::loadAnimationClip(const std::string &filePath, platformator::ReferenceResolver &resolver,
                                             bool required)
{
    if (filePath.empty())
    {
        if (required)
        {
            throw std::invalid_argument("Required animation path is empty");
        }
        return nullptr;
    }
    if (auto *cached = animations.lookupAlias(filePath))
    {
        return cached;
    }
    const auto resolved = PathManager::getInstance().resolveAssetPath(filePath);
    if (auto *cached = animations.lookupCanonical(filePath, resolved.canonicalPath))
    {
        return cached;
    }
    std::ifstream file(resolved.absolutePath);
    if (!file)
    {
        throw std::runtime_error("Cannot open animation: " + filePath);
    }
    auto clip = std::make_unique<AnimationClip>();
    try
    {
        deserialize(nlohmann::json::parse(file), *clip, resolver);
    }
    catch (const std::exception &error)
    {
        throw std::runtime_error("Animation '" + filePath + "': " + error.what());
    }
    clip->setFilePath(resolved.canonicalPath);
    owner.tagAssetRuntime(*clip);
    return animations.publish(filePath, resolved.canonicalPath, std::move(clip));
}

AnimationClip *AssetStore::findAnimationClip(const std::string &filePath)
{
    return animations.find(filePath);
}

AnimationClip *AssetStore::reloadAnimationClip(const std::string &filePath, const nlohmann::json &preparedDocument)
{
    owner.assertCanMutate();
    if (!owner.isIdle())
    {
        throw std::logic_error("asset reload requires an idle Runtime owner boundary");
    }
    if (filePath.empty())
    {
        throw std::invalid_argument("Animation path is empty");
    }
    const auto resolved = PathManager::getInstance().resolveAssetPath(filePath);
    auto replacement = std::make_unique<AnimationClip>();
    ActiveReferenceResolver resolver(platformator::Runtime::current());
    try
    {
        deserialize(preparedDocument, *replacement, resolver);
    }
    catch (const std::exception &error)
    {
        throw std::runtime_error("Animation '" + filePath + "': " + error.what());
    }

    // Deserialization can resolve a frame through a cached logical request
    // alias that still points at the prior symlink target. The editor has
    // already prepared current frame textures before this owner-thread commit;
    // bind those current wrappers into the replacement clip and rebind each
    // serialized request so future Runtime loads observe the adopted target.
    const auto preparedFrames = preparedDocument.find("frames");
    if (preparedFrames != preparedDocument.end() && preparedFrames->is_array())
    {
        const auto frameCount = std::min(preparedFrames->size(), replacement->frames.size());
        for (std::size_t index = 0; index < frameCount; ++index)
        {
            const auto &frame = (*preparedFrames)[index];
            if (!frame.is_object())
            {
                continue;
            }
            const auto texturePath = frame.find("textureWrapperFilePath");
            if (texturePath == frame.end() || !texturePath->is_string() || texturePath->get<std::string>().empty())
            {
                continue;
            }
            const auto request = texturePath->get<std::string>();
            const auto resolvedTexture = PathManager::getInstance().resolveAssetPath(request);
            const auto cachedTexture = textures.entries.find(resolvedTexture.canonicalPath);
            if (cachedTexture == textures.entries.end())
            {
                continue;
            }
            replacement->frames[index].setTextureWrapper(cachedTexture->second.get());
            textures.aliases.rebind(request, resolvedTexture.canonicalPath);
        }
    }
    replacement->setFilePath(resolved.canonicalPath);
    owner.tagAssetRuntime(*replacement);
    return animations.replace(filePath, resolved.canonicalPath, std::move(replacement));
}

void AssetStore::freeAnimationClip(AnimationClip *animationClip)
{
    if (animationClip == nullptr)
    {
        return;
    }
    PLATFORMATOR_LOG("Freeing animation clip: %s\n", animationClip->getFilePath().c_str());
    animations.free(animationClip);
}

Template *AssetStore::loadTemplate(const std::string &filePath, bool required, bool *deferredCyclic)
{
    if (filePath.empty())
    {
        if (required)
        {
            throw std::invalid_argument("Required template path is empty");
        }
        return nullptr;
    }
    const std::string normalizedRequest =
        (filePath.starts_with("templates/") || filePath.starts_with("templates\\")) ? "assets/" + filePath : filePath;
    if (auto *cached = templates.lookupAlias(filePath))
    {
        return cached;
    }
    const auto resolved = PathManager::getInstance().resolveAssetPath(normalizedRequest);
    if (auto *cached = templates.lookupCanonical(filePath, resolved.canonicalPath))
    {
        return cached;
    }
    if (activeTemplateLoads.contains(resolved.canonicalPath))
    {
        if (deferredCyclic != nullptr)
        {
            *deferredCyclic = true;
            return nullptr;
        }
        throw std::runtime_error("Cyclic template dependency: " + filePath);
    }
    std::ifstream file(resolved.absolutePath);
    if (!file)
    {
        throw std::runtime_error("Cannot open template: " + filePath);
    }
    struct ActiveLoadGuard
    {
        std::unordered_set<std::string> &active;
        std::string key;
        ~ActiveLoadGuard()
        {
            active.erase(key);
        }
    };
    activeTemplateLoads.insert(resolved.canonicalPath);
    ActiveLoadGuard activeGuard{activeTemplateLoads, resolved.canonicalPath};

    auto tpl = std::make_unique<Template>();
    try
    {
        owner.loadTemplateDocument(*tpl, nlohmann::json::parse(file));
    }
    catch (const std::exception &error)
    {
        throw std::runtime_error("Template '" + filePath + "': " + error.what());
    }
    tpl->setFilePath(resolved.canonicalPath);
    owner.tagAssetRuntime(*tpl);
    return templates.publish(filePath, resolved.canonicalPath, std::move(tpl));
}

void AssetStore::freeTemplate(Template *tpl)
{
    templates.free(tpl);
}

void AssetStore::freeAll()
{
    templates.clear();
    animations.clear();
    textures.clear();
    audio.clear();
}
} // namespace platformator_detail
