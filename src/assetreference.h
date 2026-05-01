#pragma once

#include <optional>
#include <type_traits>

#include <json.hpp>

#include "gamemanager.h"
#include "baseobject.h"

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

        BaseObject *resolvedObject = gameManager.getObjectByFilePath(*filePath);
        object = resolvedObject == nullptr ? nullptr : static_cast<T *>(resolvedObject);
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
    T *object;
    std::optional<int> objectId;
};

template <typename T>
void to_json(nlohmann::json &j, const ObjectReference<T> &reference)
{
    const std::optional<int> referenceId = reference.getReferencedId();
    if (referenceId.has_value())
    {
        j = *referenceId;
        return;
    }

    j = nullptr;
}

template <typename T>
void from_json(const nlohmann::json &j, ObjectReference<T> &reference)
{
    if (j.is_null())
    {
        reference.clear();
        return;
    }

    reference.setReferencedId(j.get<int>());
}
