#pragma once

#include <optional>
#include <type_traits>

#include <json.hpp>

#include "baseobject.h"

class GameManager;

GameManager &getGameManagerInstance();
BaseObject *resolveObjectReference(GameManager &gameManager, int objectId);

template <typename T>
class ObjectReference
{
    static_assert(std::is_base_of_v<BaseObject, T>, "ObjectReference can only store BaseObject-derived types.");

public:
    ObjectReference() : object(nullptr), objectId(std::nullopt)
    {
    }

    explicit ObjectReference(T *object) : object(nullptr), objectId(std::nullopt)
    {
        set(object);
    }

    explicit ObjectReference(int objectId) : object(nullptr), objectId(objectId)
    {
    }

    T *get() const
    {
        return object;
    }

    std::optional<int> getReferencedId() const
    {
        if (object != nullptr)
        {
            return object->getId();
        }

        return objectId;
    }

    void set(T *value)
    {
        object = value;
        objectId = value == nullptr ? std::nullopt : std::optional<int>(value->getId());
    }

    void setReferencedId(int value)
    {
        object = nullptr;
        objectId = value;
    }

    void clear()
    {
        object = nullptr;
        objectId = std::nullopt;
    }

    T *resolve(GameManager &gameManager = getGameManagerInstance())
    {
        const std::optional<int> referenceId = getReferencedId();
        if (!referenceId.has_value())
        {
            object = nullptr;
            return nullptr;
        }

        BaseObject *resolvedObject = resolveObjectReference(gameManager, *referenceId);
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
