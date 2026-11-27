#pragma once

#include <cassert>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>
#include "platformator/baseobject.h"

namespace platformator_detail
{
class ObjectRegistry
{
  public:
    ObjectRegistry();

    platformator::ObjectHandle add(BaseObject &object, ObjectKind kind, bool published = true)
    {
        assert(!object.handle);
        uint32_t index = INVALID_SLOT;
        if (freeHead == INVALID_SLOT)
        {
            if (slots.size() == INVALID_SLOT)
                throw std::overflow_error("Object handle slots exhausted");
            index = static_cast<uint32_t>(slots.size());
            slots.emplace_back();
        }
        else
        {
            index = freeHead;
            freeHead = slots[index].nextFree;
            slots[index].nextFree = INVALID_SLOT;
        }
        auto &slot = slots[index];
        slot.object = &object;
        slot.kind = kind;
        object.handle = {index, slot.generation, runtimeSerial};
        object.lifecycle = published ? ObjectLifecycle::Live : ObjectLifecycle::Staged;
        return object.handle;
    }

    void publish(BaseObject &object) noexcept
    {
        object.lifecycle = ObjectLifecycle::Live;
    }

    void retire(BaseObject &object) noexcept
    {
        object.lifecycle = ObjectLifecycle::PendingRetire;
    }

    void erase(BaseObject &object) noexcept
    {
        assert(object.handle);
        assert(object.handle.runtimeSerial == runtimeSerial);
        assert(object.handle.slot < slots.size());
        const uint32_t index = object.handle.slot;
        auto &slot = slots[index];
        assert(slot.object == &object);
        slot.object = nullptr;
        if (slot.generation != std::numeric_limits<uint64_t>::max())
        {
            ++slot.generation;
            slot.nextFree = freeHead;
            freeHead = index;
        }
        object.handle = {};
    }

    BaseObject *get(platformator::ObjectHandle handle, ObjectKind expected, bool staging = false) const noexcept
    {
        if (handle.runtimeSerial != runtimeSerial || handle.slot >= slots.size())
            return nullptr;
        const auto &slot = slots[handle.slot];
        if (!slot.object || slot.generation != handle.generation)
            return nullptr;
        const bool live = slot.object->lifecycle == ObjectLifecycle::Live;
        const bool staged = staging && slot.object->lifecycle == ObjectLifecycle::Staged;
        if (!live && !staged)
            return nullptr;
        return objectKindMatches(slot.kind, expected) ? slot.object : nullptr;
    }

    uint64_t serial() const noexcept
    {
        return runtimeSerial;
    }

  private:
    static constexpr uint32_t INVALID_SLOT = std::numeric_limits<uint32_t>::max();

    struct Slot
    {
        BaseObject *object = nullptr;
        uint64_t generation = 1;
        ObjectKind kind = ObjectKind::BaseObject;
        uint32_t nextFree = INVALID_SLOT;
    };

    uint64_t runtimeSerial;
    uint32_t freeHead = INVALID_SLOT;
    // Monotonic slot table: entries are never removed so retired generations reject stale handles.
    std::vector<Slot> slots;
};
} // namespace platformator_detail
