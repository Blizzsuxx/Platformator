#pragma once

#include <vector>
#include "platformator/baseobject.h"

namespace platformator_detail
{
class ObjectRegistry
{
  public:
    ObjectRegistry();
    platformator::ObjectHandle add(BaseObject &object, ObjectKind kind, bool published = true);
    void publish(BaseObject &object);
    void retire(BaseObject &object);
    void erase(BaseObject &object);
    BaseObject *get(platformator::ObjectHandle handle, ObjectKind expected, bool staging = false) const;
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
        bool published = false;
    };
    uint64_t runtimeSerial;
    std::vector<Slot> slots;
    std::vector<uint32_t> freeSlots;
};
} // namespace platformator_detail
