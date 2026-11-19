#pragma once

#include <cassert>
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
        uint32_t index;
        if (freeSlots.empty())
        {
            if (slots.size() == std::numeric_limits<uint32_t>::max())
                throw std::overflow_error("Object handle slots exhausted");
            index = static_cast<uint32_t>(slots.size());
            slots.emplace_back();
        }
        else
        {
            index = freeSlots.back();
            freeSlots.pop_back();
        }
        auto &slot = slots[index];
        slot.object = &object;
        slot.kind = kind;
        object.handle = {index, slot.generation, runtimeSerial};
        object.lifecycle = published ? ObjectLifecycle::Live : ObjectLifecycle::Staged;
        return object.handle;
    }

    void publish(BaseObject &object)
    {
        object.lifecycle = ObjectLifecycle::Live;
    }

    void retire(BaseObject &object)
    {
        object.lifecycle = ObjectLifecycle::PendingRetire;
    }

    void erase(BaseObject &object)
    {
        auto &slot = slots[object.handle.slot];
        slot.object = nullptr;
        if (slot.generation != std::numeric_limits<uint64_t>::max())
        {
            ++slot.generation;
            freeSlots.push_back(object.handle.slot);
        }
        object.handle = {};
    }

    BaseObject *get(platformator::ObjectHandle handle, ObjectKind expected, bool staging = false) const
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
        const bool matches = expected == ObjectKind::BaseObject || expected == slot.kind ||
                             (expected == ObjectKind::Component && slot.kind != ObjectKind::GameObject) ||
                             (expected == ObjectKind::Collider &&
                              (slot.kind == ObjectKind::BoxCollider || slot.kind == ObjectKind::CircleCollider));
        return matches ? slot.object : nullptr;
    }

    uint64_t serial() const
    {
        return runtimeSerial;
    }

  private:
    struct Slot
    {
        BaseObject *object = nullptr;
        uint64_t generation = 1;
        ObjectKind kind = ObjectKind::BaseObject;
    };
    uint64_t runtimeSerial;
    std::vector<Slot> slots;
    std::vector<uint32_t> freeSlots;
};
} // namespace platformator_detail
