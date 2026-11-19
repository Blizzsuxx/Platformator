#include "objectregistry.h"

#include <atomic>
#include <cassert>
#include <limits>
#include <stdexcept>

namespace platformator_detail
{
ObjectRegistry::ObjectRegistry()
{
    static std::atomic<uint64_t> nextSerial{1};
    runtimeSerial = nextSerial.load(std::memory_order_relaxed);
    do
    {
        if (runtimeSerial == std::numeric_limits<uint64_t>::max())
            throw std::overflow_error("Runtime identity exhausted");
    } while (!nextSerial.compare_exchange_weak(runtimeSerial, runtimeSerial + 1, std::memory_order_relaxed));
}

platformator::ObjectHandle ObjectRegistry::add(BaseObject &object, ObjectKind kind, bool published)
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

void ObjectRegistry::publish(BaseObject &object)
{
    object.lifecycle = ObjectLifecycle::Live;
}
void ObjectRegistry::retire(BaseObject &object)
{
    object.lifecycle = ObjectLifecycle::PendingRetire;
}
void ObjectRegistry::erase(BaseObject &object)
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
BaseObject *ObjectRegistry::get(platformator::ObjectHandle handle, ObjectKind expected, bool staging) const
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
} // namespace platformator_detail
