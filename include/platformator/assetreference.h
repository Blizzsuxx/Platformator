#pragma once

#include <optional>
#include <string>
#include <type_traits>

#include <json.hpp>

#include "platformator/detail/animationclip.h"
#include "platformator/detail/audiowrapper.h"
#include "platformator/detail/baseobject.h"
#include "platformator/detail/texturewrapper.h"
#include "platformator/runtime.h"

namespace platformator_detail
{
    TextureWrapper *loadTextureAssetReference(platformator::Runtime &runtime, const std::string &assetPath);
    AudioWrapper *loadAudioAssetReference(platformator::Runtime &runtime, const std::string &assetPath);
    AnimationClip *loadAnimationClipAssetReference(platformator::Runtime &runtime, const std::string &assetPath);
}

template <typename T>
inline constexpr bool always_false_asset_reference_v = false;

template <typename T>
class AssetReference
{
    static_assert(std::is_base_of_v<Asset, T>, "AssetReference can only store Asset-derived types.");

public:
    AssetReference() : object(nullptr), filePath(std::nullopt)
    {
    }

    AssetReference(const AssetReference &other) : object(other.object), filePath(other.filePath)
    {
        retainObject();
    }

    AssetReference(AssetReference &&other) noexcept : object(other.object), filePath(std::move(other.filePath))
    {
        other.object = nullptr;
        other.filePath = std::nullopt;
    }

    explicit AssetReference(T *object) : object(nullptr), filePath(std::nullopt)
    {
        set(object);
    }

    explicit AssetReference(const std::string &filePath) : object(nullptr), filePath(filePath)
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

        releaseObject();
        object = other.object;
        filePath = other.filePath;
        retainObject();
        return *this;
    }

    AssetReference &operator=(AssetReference &&other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }

        releaseObject();
        object = other.object;
        filePath = std::move(other.filePath);
        other.object = nullptr;
        other.filePath = std::nullopt;
        return *this;
    }

    T *get() const
    {
        return object;
    }

    std::optional<std::string> getFilePath() const
    {
        if (object != nullptr)
        {
            return object->getFilePath();
        }

        return filePath;
    }

    void set(T *value)
    {
        if (object == value)
        {
            filePath = value == nullptr ? std::nullopt : std::optional<std::string>(value->getFilePath());
            return;
        }

        releaseObject();
        object = value;
        filePath = value == nullptr ? std::nullopt : std::optional<std::string>(value->getFilePath());
        retainObject();
    }

    void setFilePath(const std::string &value)
    {
        releaseObject();
        object = nullptr;
        filePath = value;
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

    T &operator*() const
    {
        return *object;
    }

    T *operator->() const
    {
        return object;
    }

    explicit operator bool() const
    {
        return object != nullptr;
    }

private:
    void retainObject()
    {
        if (object != nullptr)
        {
            object->addReference();
        }
    }

    void releaseObject()
    {
        if (object != nullptr)
        {
            T *currentObject = object;
            object = nullptr;
            currentObject->removeReferenceAndFreeIfNoReferences();
        }
    }

    static T *loadAsset(platformator::Runtime &runtime, const std::string &assetPath)
    {
        if constexpr (std::is_same_v<T, TextureWrapper>)
        {
            return platformator_detail::loadTextureAssetReference(runtime, assetPath);
        }
        else if constexpr (std::is_same_v<T, AudioWrapper>)
        {
            return platformator_detail::loadAudioAssetReference(runtime, assetPath);
        }
        else if constexpr (std::is_same_v<T, AnimationClip>)
        {
            return platformator_detail::loadAnimationClipAssetReference(runtime, assetPath);
        }
        else
        {
            static_assert(always_false_asset_reference_v<T>, "AssetReference does not know how to load this asset type.");
        }
    }

    T *object;
    std::optional<std::string> filePath;
};

template <typename T>
void to_json(nlohmann::json &j, const AssetReference<T> &reference)
{
    const std::optional<std::string> assetPath = reference.getFilePath();
    if (assetPath.has_value())
    {
        j = *assetPath;
        return;
    }

    j = nullptr;
}

template <typename T>
void from_json(const nlohmann::json &j, AssetReference<T> &reference)
{
    if (j.is_null())
    {
        reference.clear();
        return;
    }

    reference.setFilePath(j.get<std::string>());
    reference.resolve();
}

namespace platformator
{
    using TextureAssetRef = AssetReference<TextureWrapper>;
    using AudioAssetRef = AssetReference<AudioWrapper>;
    using AnimationClipRef = AssetReference<AnimationClip>;
} // namespace platformator