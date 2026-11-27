#pragma once

#include <optional>
#include <cassert>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <json.hpp>

#include "platformator/baseobject.h"
#include "platformator/animationclip.h"
#include "platformator/audiowrapper.h"
#include "platformator/runtime.h"
#include "platformator/referenceresolver.h"
#include "platformator/template.h"
#include <stdexcept>
#include "platformator/texturewrapper.h"

template <typename T> inline constexpr bool always_false_asset_reference_v = false;

template <typename T> class AssetReference
{
    static_assert(std::is_base_of_v<Asset, T>, "AssetReference can only store Asset-derived types.");

  public:
    AssetReference() : object(nullptr), filePath(std::nullopt)
    {
    }

    AssetReference(const AssetReference &other)
        : object(other.get()), filePath(other.filePath), runtimeSerial(other.runtimeSerial)
    {
        retainObject();
    }

    AssetReference(AssetReference &&other) noexcept
        : object(other.object), filePath(std::move(other.filePath)), runtimeSerial(other.runtimeSerial)
    {
        other.object = nullptr;
        other.runtimeSerial = 0;
        other.filePath = std::nullopt;
    }

    explicit AssetReference(T *object) : object(nullptr), filePath(std::nullopt)
    {
        set(object);
    }

    explicit AssetReference(const std::string &filePath)
        : object(nullptr),
          filePath(filePath.empty() ? std::nullopt : std::optional<std::string>(filePath))
    {
    }

    ~AssetReference()
    {
        releaseObject();
    }

    AssetReference &operator=(const AssetReference &other)
    {
        if (this == &other)
        {
            return *this;
        }

        AssetReference copy(other);
        return *this = std::move(copy);
    }

    AssetReference &operator=(AssetReference &&other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }

        releaseObject();
        object = other.object;
        runtimeSerial = other.runtimeSerial;
        filePath = std::move(other.filePath);
        other.object = nullptr;
        other.runtimeSerial = 0;
        other.filePath = std::nullopt;
        return *this;
    }

    T *get() const
    {
        assertLifetime();
        return object;
    }

    std::optional<std::string> getFilePath() const
    {
        assertLifetime();
        if (object != nullptr)
        {
            return object->getFilePath();
        }

        return filePath;
    }

    bool hasFilePath() const
    {
        assertLifetime();
        return object != nullptr || filePath.has_value();
    }

    std::string_view getFilePathView() const
    {
        assertLifetime();
        if (object != nullptr)
            return object->getFilePath();
        return filePath ? std::string_view(*filePath) : std::string_view{};
    }

    uint64_t getRuntimeSerial() const
    {
        assertLifetime();
        return runtimeSerial;
    }

    void set(T *value)
    {
        assertLifetime();
        if (object == value)
        {
            if (value == nullptr)
            {
                filePath.reset();
            }
            else if (!filePath || *filePath != value->getFilePath())
            {
                filePath = value->getFilePath();
            }
            return;
        }

        // Retain first so a failed path copy cannot leave an unowned pointer or
        // strand a freshly loaded, zero-reference resource in the cache.
        if (value) value->addReference();
        std::optional<std::string> newPath;
        try
        {
            if (value) newPath = value->getFilePath();
        }
        catch (...)
        {
            if (value) value->removeReferenceAndFreeIfNoReferences();
            throw;
        }
        const auto newSerial = value ? value->getRuntimeSerial() : 0;
        releaseObject();
        object = value;
        runtimeSerial = newSerial;
        filePath = std::move(newPath);
    }

    void setFilePath(const std::string &value)
    {
        if (value.empty())
        {
            clear();
            return;
        }
        std::optional<std::string> newPath(value);
        releaseObject();
        filePath = std::move(newPath);
    }

    void clear()
    {
        releaseObject();
        filePath = std::nullopt;
    }

    T *resolve()
    {
        return resolve(platformator::Runtime::current());
    }

    T *resolve(platformator::Runtime &runtime)
    {
        const std::optional<std::string> resolvedFilePath = getFilePath();
        if (!resolvedFilePath.has_value())
        {
            releaseObject();
            return nullptr;
        }

        set(loadAsset(runtime, *resolvedFilePath));
        return object;
    }

    T *resolve(platformator::ReferenceResolver &resolver)
    {
        if (object != nullptr)
            return get();
        const auto path = getFilePath();
        if (!path)
            return nullptr;
        T *asset;
        if constexpr (std::is_same_v<T, TextureWrapper>)
            asset = resolver.loadTexture(*path);
        else if constexpr (std::is_same_v<T, AudioWrapper>)
            asset = resolver.loadAudio(*path);
        else if constexpr (std::is_same_v<T, AnimationClip>)
            asset = resolver.loadAnimationClip(*path);
        else if constexpr (std::is_same_v<T, Template>)
        {
            asset = resolver.loadTemplate(*path);
            if (!asset && resolver.allowDeferredTemplate(*path))
                return nullptr;
        }
        else
            static_assert(always_false_asset_reference_v<T>);
        if (!asset)
            throw std::runtime_error("Required asset failed to load: " + *path);
        set(asset);
        return object;
    }

    T &operator*() const
    {
        return *get();
    }

    T *operator->() const
    {
        return get();
    }

    explicit operator bool() const
    {
        return get() != nullptr;
    }

  private:
    void retainObject()
    {
        assertLifetime();
        if (object != nullptr)
        {
            object->addReference();
        }
    }

    void releaseObject()
    {
        assertLifetime();
        if (object != nullptr)
        {
            T *currentObject = object;
            object = nullptr;
            runtimeSerial = 0;
            currentObject->removeReferenceAndFreeIfNoReferences();
        }
    }

    static T *loadAsset(platformator::Runtime &runtime, const std::string &assetPath)
    {
        if constexpr (std::is_same_v<T, TextureWrapper>)
            return runtime.loadTexture(assetPath);
        else if constexpr (std::is_same_v<T, AudioWrapper>)
            return runtime.loadAudio(assetPath);
        else if constexpr (std::is_same_v<T, AnimationClip>)
            return runtime.loadAnimationClip(assetPath);
        else if constexpr (std::is_same_v<T, Template>)
            return runtime.loadTemplate(assetPath);
        else
            static_assert(always_false_asset_reference_v<T>,
                          "AssetReference does not know how to load this asset type.");
    }

    void assertLifetime() const
    {
#ifndef NDEBUG
        if (runtimeSerial)
        {
            const auto *runtime = platformator::Runtime::tryGetCurrent();
            assert(runtime && runtime->getRuntimeSerial() == runtimeSerial &&
                   "Asset references must not outlive their runtime");
        }
#endif
    }
    T *object;
    std::optional<std::string> filePath;
    uint64_t runtimeSerial = 0;
};

template <typename T> void to_json(nlohmann::json &j, const AssetReference<T> &reference)
{
    const std::optional<std::string> assetPath = reference.getFilePath();
    if (assetPath.has_value())
    {
        j = *assetPath;
        return;
    }

    j = nullptr;
}

template <typename T> void from_json(const nlohmann::json &j, AssetReference<T> &reference)
{
    if (j.is_null())
    {
        reference.clear();
        return;
    }

    reference.setFilePath(j.get<std::string>());
}

namespace platformator
{
using TextureAssetRef = AssetReference<TextureWrapper>;
using AudioAssetRef = AssetReference<AudioWrapper>;
using AnimationClipRef = AssetReference<AnimationClip>;
using TemplateAssetRef = AssetReference<Template>;

namespace detail
{
inline Template *requireTemplateAsset(const AssetReference<Template> &tmpl)
{
    if (Template *resolved = tmpl.get())
        return resolved;
    const auto path = tmpl.getFilePath();
    if (!path || path->empty())
        throw std::invalid_argument("Cannot instantiate an unset template reference");
    return Runtime::current().loadTemplate(*path);
}
} // namespace detail

inline GameObject *instantiate(const AssetReference<Template> &tmpl, GameObject *parent = nullptr)
{
    return detail::requireTemplateAsset(tmpl)->instantiate(parent);
}

inline GameObject *instantiate(const AssetReference<Template> &tmpl, const Eigen::Vector2f &worldPosition,
                               float worldRotation, GameObject *parent = nullptr)
{
    return detail::requireTemplateAsset(tmpl)->instantiate(worldPosition, worldRotation, parent);
}

inline GameObject *instantiate(const AssetReference<Template> &tmpl, const Eigen::Vector2f &worldPosition,
                               GameObject *parent = nullptr)
{
    return detail::requireTemplateAsset(tmpl)->instantiate(worldPosition, parent);
}
} // namespace platformator
