#pragma once

#include <cassert>
#include <cstddef>
#include <functional>

#include "core/flathash.h"
#include "core/flatmap.h"

class BaseObject;

namespace platformator_detail
{
// Open-addressing linear-probing ID table with backward-shift deletion (no tombstones).
// Uses value == nullptr as the empty sentinel so all 32-bit signed file IDs are valid keys.
class FlatIdMap
{
  public:
    FlatIdMap() = default;
    FlatIdMap(const FlatIdMap &) = default;
    FlatIdMap &operator=(const FlatIdMap &) = default;
    FlatIdMap(FlatIdMap &&) noexcept = default;
    FlatIdMap &operator=(FlatIdMap &&) noexcept = default;

    BaseObject *find(int key) const noexcept
    {
        return map.find(key);
    }

    void insertOrAssign(int key, BaseObject *value)
    {
        assert(value != nullptr);
        if (value == nullptr)
        {
            map.erase(key);
            return;
        }
        map.insertOrAssign(key, value);
    }

    void erase(int key) noexcept
    {
        map.erase(key);
    }

    void reserve(size_t expected)
    {
        map.reserve(expected);
    }

    void clear() noexcept
    {
        map.clear();
    }

    void swap(FlatIdMap &other) noexcept
    {
        map.swap(other.map);
    }

    size_t size() const noexcept
    {
        return map.size();
    }

    bool empty() const noexcept
    {
        return map.empty();
    }

  private:
    FlatHashMap<int, BaseObject *, FlatInt32Hash, std::equal_to<int>, 16> map;
};
} // namespace platformator_detail
