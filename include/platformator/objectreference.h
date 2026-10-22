#pragma once

#include <cassert>
#include <optional>
#include <type_traits>
#include <json.hpp>
#include "platformator/baseobject.h"
#include "platformator/runtime.h"
#include "platformator/referenceresolver.h"
#include <stdexcept>

namespace platformator_detail
{
void validateSceneReference(const BaseObject *object);
}

template <typename T> class ObjectReference
{
    static_assert(std::is_base_of_v<BaseObject, T>);

  public:
    ObjectReference() = default;
    platformator::ObjectHandle getHandle() const
    {
        return handle;
    }
    explicit ObjectReference(T *object)
    {
        set(object);
    }
    explicit ObjectReference(int id) : objectId(id)
    {
    }
    T *get() const
    {
        auto *runtime = platformator::Runtime::tryGetCurrent();
        return runtime ? static_cast<T *>(runtime->resolveHandle(handle, platformator_detail::objectKind<T>()))
                       : nullptr;
    }
    std::optional<int> getReferencedId() const
    {
        if (!handle)
            return objectId;
        auto *object = get();
        return object ? std::optional<int>(object->getId()) : std::nullopt;
    }
    void set(T *object)
    {
        handle = object ? object->getHandle() : platformator::ObjectHandle{};
        objectId = object ? std::optional<int>(object->getId()) : std::nullopt;
    }
    void setReferencedId(int id)
    {
        handle = {};
        objectId = id;
    }
    void clear()
    {
        handle = {};
        objectId.reset();
    }
    T *resolve()
    {
        auto *runtime = platformator::Runtime::tryGetCurrent();
        return runtime ? resolve(*runtime) : nullptr;
    }
    T *resolve(platformator::Runtime &runtime)
    {
        if (handle)
            return static_cast<T *>(runtime.resolveHandle(handle, platformator_detail::objectKind<T>()));
        if (!objectId)
            return nullptr;
        auto *target = runtime.getObjectById(*objectId);
        if (!target)
            return nullptr;
        auto *typed = runtime.resolveHandle(target->getHandle(), platformator_detail::objectKind<T>());
        if (typed)
            handle = typed->getHandle();
        return static_cast<T *>(typed);
    }
    T &operator*() const
    {
        auto *object = get();
        assert(object);
        return *object;
    }
    T *resolve(platformator::ReferenceResolver &resolver)
    {
        if (!handle && !objectId)
            return nullptr;
        auto *target = handle ? resolver.findHandle(handle, platformator_detail::objectKind<T>())
                              : resolver.findObject(*objectId, platformator_detail::objectKind<T>());
        if (!target)
            throw std::runtime_error("Missing reference target or wrong kind for ID " +
                                     std::to_string(objectId.value_or(-1)));
        handle = target->getHandle();
        return static_cast<T *>(target);
    }
    T *operator->() const
    {
        auto *object = get();
        assert(object);
        return object;
    }
    explicit operator bool() const
    {
        return get() != nullptr;
    }

  private:
    platformator::ObjectHandle handle;
    std::optional<int> objectId;
};

template <typename T> void to_json(nlohmann::json &j, const ObjectReference<T> &reference)
{
    if (auto *object = reference.get())
    {
        platformator_detail::validateSceneReference(object);
        j = object->getId();
    }
    else
    {
        if (!reference.getHandle() && reference.getReferencedId())
            throw std::runtime_error("Cannot serialize an unbound object reference; resolve its file ID first");
        j = nullptr;
    }
}
template <typename T> void from_json(const nlohmann::json &j, ObjectReference<T> &reference)
{
    if (j.is_null())
        reference.clear();
    else
        reference.setReferencedId(j.get<int>());
}
