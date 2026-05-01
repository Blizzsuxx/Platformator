#pragma once

#include <optional>
#include <string>
#include <type_traits>

#include <json.hpp>

#include "baseobject.h"
#include "gamemanager.h"

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

    explicit AssetReference(T *object) : object(nullptr), filePath(std::nullopt)
    {
        set(object);
    }

    explicit AssetReference(const std::string &filePath) : object(nullptr), filePath(filePath)
    {
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
        object = value;
        filePath = value == nullptr ? std::nullopt : std::optional<std::string>(value->getFilePath());
    }

    void setFilePath(const std::string &value)
    {
        object = nullptr;
        filePath = value;
    }

    void clear()
    {
        object = nullptr;
        filePath = std::nullopt;
    }

    T *resolve(GameManager &gameManager = GameManager::getInstance())
    {
        const std::optional<std::string> filePath = getFilePath();
        if (!filePath.has_value())
        {
            object = nullptr;
            return nullptr;
        }

        object = loadAsset(gameManager, *filePath);
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
    static T *loadAsset(GameManager &gameManager, const std::string &assetPath)
    {
        if constexpr (std::is_same_v<T, TextureWrapper>)
        {
            return gameManager.loadTexture(assetPath);
        }

        if constexpr (std::is_same_v<T, AudioWrapper>)
        {
            return gameManager.loadAudio(assetPath);
        }

        if constexpr (std::is_same_v<T, AnimationClip>)
        {
            return gameManager.loadAnimationClip(assetPath);
        }

        static_assert(always_false_asset_reference_v<T>, "AssetReference does not know how to load this asset type.");
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
